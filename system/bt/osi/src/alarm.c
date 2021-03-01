/******************************************************************************
 *
 *  Copyright (C) 2014 Google, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

#define LOG_TAG "bt_osi_alarm"

#include <assert.h>
#include <errno.h>
#include <hardware/bluetooth.h>
#include <inttypes.h>
#include <malloc.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include "osi/include/alarm.h"
#include "osi/include/allocator.h"
#include "osi/include/list.h"
#include "osi/include/log.h"
#include "osi/include/osi.h"
#include "osi/include/semaphore.h"
#include "osi/include/thread.h"

// Make callbacks run at high thread priority. Some callbacks are used for audio
// related timer tasks as well as re-transmissions etc. Since we at this point
// cannot differentiate what callback we are dealing with, assume high priority
// for now.
// TODO(eisenbach): Determine correct thread priority (from parent?/per alarm?)
static const int CALLBACK_THREAD_PRIORITY_HIGH = -19;

struct alarm_t {
  // The lock is held while the callback for this alarm is being executed.
  // It allows us to release the coarse-grained monitor lock while a potentially
  // long-running callback is executing. |alarm_cancel| uses this lock to provide
  // a guarantee to its caller that the callback will not be in progress when it
  // returns.
  pthread_mutex_t callback_lock;
  period_ms_t created;
  period_ms_t period;
  period_ms_t deadline;
  bool is_periodic;
  alarm_callback_t callback;
  void *data;
};

extern bt_os_callouts_t *bt_os_callouts;

// If the next wakeup time is less than this threshold, we should acquire
// a wakelock instead of setting a wake alarm so we're not bouncing in
// and out of suspend frequently. This value is externally visible to allow
// unit tests to run faster. It should not be modified by production code.
int64_t TIMER_INTERVAL_FOR_WAKELOCK_IN_MS = 3000;
static const clockid_t CLOCK_ID = CLOCK_BOOTTIME;
static const char *WAKE_LOCK_ID = "bluedroid_timer";

// This mutex ensures that the |alarm_set|, |alarm_cancel|, and alarm callback
// functions execute serially and not concurrently. As a result, this mutex also
// protects the |alarms| list.
static pthread_mutex_t monitor;
static list_t *alarms;
static timer_t timer;
static bool timer_set;

// All alarm callbacks are dispatched from |callback_thread|
static thread_t *callback_thread;
static bool callback_thread_active;
static semaphore_t *alarm_expired;

static bool lazy_initialize(void);
static period_ms_t now(void);
static void alarm_set_internal(alarm_t *alarm, period_ms_t deadline, alarm_callback_t cb, void *data, bool is_periodic);
static void schedule_next_instance(alarm_t *alarm, bool force_reschedule);
static void reschedule_root_alarm(void);
static void timer_callback(void *data);
static void callback_dispatch(void *context);

alarm_t *alarm_new(void) {
  // Make sure we have a list we can insert alarms into.
  if (!alarms && !lazy_initialize())
    return NULL;

  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);

  alarm_t *ret = osi_calloc(sizeof(alarm_t));
  if (!ret) {
    LOG_ERROR("%s unable to allocate memory for alarm.", __func__);
    goto error;
  }

  // Make this a recursive mutex to make it safe to call |alarm_cancel| from
  // within the callback function of the alarm.
  int error = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  if (error) {
    LOG_ERROR("%s unable to create a recursive mutex: %s", __func__, strerror(error));
    goto error;
  }

  error = pthread_mutex_init(&ret->callback_lock, &attr);
  if (error) {
    LOG_ERROR("%s unable to initialize mutex: %s", __func__, strerror(error));
    goto error;
  }

  pthread_mutexattr_destroy(&attr);
  return ret;

error:;
  pthread_mutexattr_destroy(&attr);
  osi_free(ret);
  return NULL;
}

void alarm_free(alarm_t *alarm) {
  if (!alarm)
    return;

  alarm_cancel(alarm);
  pthread_mutex_destroy(&alarm->callback_lock);
  osi_free(alarm);
}

period_ms_t alarm_get_remaining_ms(const alarm_t *alarm) {
  assert(alarm != NULL);
  period_ms_t remaining_ms = 0;

  pthread_mutex_lock(&monitor);
  if (alarm->deadline)
    remaining_ms = alarm->deadline - now();
  pthread_mutex_unlock(&monitor);

  return remaining_ms;
}

void alarm_set(alarm_t *alarm, period_ms_t deadline, alarm_callback_t cb, void *data) {
  alarm_set_internal(alarm, deadline, cb, data, false);
}

void alarm_set_periodic(alarm_t *alarm, period_ms_t period, alarm_callback_t cb, void *data) {
  alarm_set_internal(alarm, period, cb, data, true);
}

// Runs in exclusion with alarm_cancel and timer_callback.
static void alarm_set_internal(alarm_t *alarm, period_ms_t period, alarm_callback_t cb, void *data, bool is_periodic) {
  assert(alarms != NULL);
  assert(alarm != NULL);
  assert(cb != NULL);

  pthread_mutex_lock(&monitor);

  alarm->created = now();
  alarm->is_periodic = is_periodic;
  alarm->period = period;
  alarm->callback = cb;
  alarm->data = data;

  schedule_next_instance(alarm, false);

  pthread_mutex_unlock(&monitor);
}

void alarm_cancel(alarm_t *alarm) {
  assert(alarms != NULL);
  assert(alarm != NULL);

  pthread_mutex_lock(&monitor);

  bool needs_reschedule = (!list_is_empty(alarms) && list_front(alarms) == alarm);

  list_remove(alarms, alarm);
  alarm->deadline = 0;
  alarm->callback = NULL;
  alarm->data = NULL;

  if (needs_reschedule)
    reschedule_root_alarm();

  pthread_mutex_unlock(&monitor);

  // If the callback for |alarm| is in progress, wait here until it completes.
  pthread_mutex_lock(&alarm->callback_lock);
  pthread_mutex_unlock(&alarm->callback_lock);
}

void alarm_cleanup(void) {
  // If lazy_initialize never ran there is nothing to do
  if (!alarms)
    return;

  callback_thread_active = false;
  semaphore_post(alarm_expired);
  thread_free(callback_thread);
  callback_thread = NULL;

  semaphore_free(alarm_expired);
  alarm_expired = NULL;
  timer_delete(&timer);
  list_free(alarms);
  alarms = NULL;

  pthread_mutex_destroy(&monitor);
}

static bool lazy_initialize(void) {
  assert(alarms == NULL);

  pthread_mutex_init(&monitor, NULL);

  alarms = list_new(NULL);
  if (!alarms) {
    LOG_ERROR("%s unable to allocate alarm list.", __func__);
    return false;
  }

  struct sigevent sigevent;
  memset(&sigevent, 0, sizeof(sigevent));
  sigevent.sigev_notify = SIGEV_THREAD;
  sigevent.sigev_notify_function = (void (*)(union sigval))timer_callback;
  if (timer_create(CLOCK_ID, &sigevent, &timer) == -1) {
    LOG_ERROR("%s unable to create timer: %s", __func__, strerror(errno));
    return false;
  }

  alarm_expired = semaphore_new(0);
  if (!alarm_expired) {
    LOG_ERROR("%s unable to create alarm expired semaphore", __func__);
    return false;
  }

  callback_thread_active = true;
  callback_thread = thread_new("alarm_callbacks");
  if (!callback_thread) {
    LOG_ERROR("%s unable to create alarm callback thread.", __func__);
    return false;
  }

  thread_set_priority(callback_thread, CALLBACK_THREAD_PRIORITY_HIGH);
  thread_post(callback_thread, callback_dispatch, NULL);
  return true;
}

static period_ms_t now(void) {
  assert(alarms != NULL);

  struct timespec ts;
  if (clock_gettime(CLOCK_ID, &ts) == -1) {
    LOG_ERROR("%s unable to get current time: %s", __func__, strerror(errno));
    return 0;
  }

  return (ts.tv_sec * 1000LL) + (ts.tv_nsec / 1000000LL);
}

// Must be called with monitor held
static void schedule_next_instance(alarm_t *alarm, bool force_reschedule) {
  // If the alarm is currently set and it's at the start of the list,
  // we'll need to re-schedule since we've adjusted the earliest deadline.
  bool needs_reschedule = (!list_is_empty(alarms) && list_front(alarms) == alarm);
  if (alarm->callback)
    list_remove(alarms, alarm);

  // Calculate the next deadline for this alarm
  period_ms_t just_now = now();
  period_ms_t ms_into_period = alarm->is_periodic ? ((just_now - alarm->created) % alarm->period) : 0;
  alarm->deadline = just_now + (alarm->period - ms_into_period);

  // Add it into the timer list sorted by deadline (earliest deadline first).
  if (list_is_empty(alarms) || ((alarm_t *)list_front(alarms))->deadline >= alarm->deadline)
    list_prepend(alarms, alarm);
  else
    for (list_node_t *node = list_begin(alarms); node != list_end(alarms); node = list_next(node)) {
      list_node_t *next = list_next(node);
      if (next == list_end(alarms) || ((alarm_t *)list_node(next))->deadline >= alarm->deadline) {
        list_insert_after(alarms, node, alarm);
        break;
      }
    }

  // If the new alarm has the earliest deadline, we need to re-evaluate our schedule.
  if (force_reschedule || needs_reschedule || (!list_is_empty(alarms) && list_front(alarms) == alarm))
    reschedule_root_alarm();
}

// NOTE: must be called with monitor lock.
static void reschedule_root_alarm(void) {
  bool timer_was_set = timer_set;
  assert(alarms != NULL);

  // If used in a zeroed state, disarms the timer
  struct itimerspec wakeup_time;
  memset(&wakeup_time, 0, sizeof(wakeup_time));

  if (list_is_empty(alarms))
    goto done;

  alarm_t *next = list_front(alarms);
  int64_t next_expiration = next->deadline - now();
  if (next_expiration < TIMER_INTERVAL_FOR_WAKELOCK_IN_MS) {
    if (!timer_set) {
      int status = bt_os_callouts->acquire_wake_lock(WAKE_LOCK_ID);
      if (status != BT_STATUS_SUCCESS) {
        LOG_ERROR("%s unable to acquire wake lock: %d", __func__, status);
        goto done;
      }
    }

    wakeup_time.it_value.tv_sec = (next->deadline / 1000);
    wakeup_time.it_value.tv_nsec = (next->deadline % 1000) * 1000000LL;
  } else {
    if (!bt_os_callouts->set_wake_alarm(next_expiration, true, timer_callback, NULL))
      LOG_ERROR("%s unable to set wake alarm for %" PRId64 "ms.", __func__, next_expiration);
  }

done:
  timer_set = wakeup_time.it_value.tv_sec != 0 || wakeup_time.it_value.tv_nsec != 0;
  if (timer_was_set && !timer_set) {
    bt_os_callouts->release_wake_lock(WAKE_LOCK_ID);
  }

  if (timer_settime(timer, TIMER_ABSTIME, &wakeup_time, NULL) == -1)
    LOG_ERROR("%s unable to set timer: %s", __func__, strerror(errno));

  // If next expiration was in the past (e.g. short timer that got context switched)
  // then the timer might have diarmed itself. Detect this case and work around it
  // by manually signalling the |alarm_expired| semaphore.
  //
  // It is possible that the timer was actually super short (a few milliseconds)
  // and the timer expired normally before we called |timer_gettime|. Worst case,
  // |alarm_expired| is signaled twice for that alarm. Nothing bad should happen in
  // that case though since the callback dispatch function checks to make sure the
  // timer at the head of the list actually expired.
  if (timer_set) {
    struct itimerspec time_to_expire;
    timer_gettime(timer, &time_to_expire);
    if (time_to_expire.it_value.tv_sec == 0 && time_to_expire.it_value.tv_nsec == 0) {
      LOG_ERROR("%s alarm expiration too close for posix timers, switching to guns", __func__);
      semaphore_post(alarm_expired);
    }
  }
}

// Callback function for wake alarms and our posix timer
static void timer_callback(UNUSED_ATTR void *ptr) {
  semaphore_post(alarm_expired);
}

// Function running on |callback_thread| that dispatches alarm callbacks upon
// alarm expiration, which is signaled using |alarm_expired|.
static void callback_dispatch(UNUSED_ATTR void *context) {
  while (true) {
    semaphore_wait(alarm_expired);
    if (!callback_thread_active)
      break;

    pthread_mutex_lock(&monitor);
    alarm_t *alarm;

    // Take into account that the alarm may get cancelled before we get to it.
    // We're done here if there are no alarms or the alarm at the front is in
    // the future. Release the monitor lock and exit right away since there's
    // nothing left to do.
    if (list_is_empty(alarms) || (alarm = list_front(alarms))->deadline > now()) {
      reschedule_root_alarm();
      pthread_mutex_unlock(&monitor);
      continue;
    }

    list_remove(alarms, alarm);

    alarm_callback_t callback = alarm->callback;
    void *data = alarm->data;

    if (alarm->is_periodic) {
      schedule_next_instance(alarm, true);
    } else {
      reschedule_root_alarm();

      alarm->deadline = 0;
      alarm->callback = NULL;
      alarm->data = NULL;
    }

    // Downgrade lock.
    pthread_mutex_lock(&alarm->callback_lock);
    pthread_mutex_unlock(&monitor);

    callback(data);

    pthread_mutex_unlock(&alarm->callback_lock);
  }

  LOG_DEBUG("%s Callback thread exited", __func__);
}
