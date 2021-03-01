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

#include <gtest/gtest.h>
#include <hardware/bluetooth.h>
#include <unistd.h>

#include "AlarmTestHarness.h"

extern "C" {
#include "alarm.h"
#include "allocation_tracker.h"
}

static timer_t timer;
static alarm_cb saved_callback;
static void *saved_data;
static AlarmTestHarness *current_harness;

static void timer_callback(void *) {
  saved_callback(saved_data);
}

void AlarmTestHarness::SetUp() {
  AllocationTestHarness::SetUp();

  current_harness = this;
  TIMER_INTERVAL_FOR_WAKELOCK_IN_MS = 100;
  lock_count = 0;

  struct sigevent sigevent;
  memset(&sigevent, 0, sizeof(sigevent));
  sigevent.sigev_notify = SIGEV_THREAD;
  sigevent.sigev_notify_function = (void (*)(union sigval))timer_callback;
  sigevent.sigev_value.sival_ptr = NULL;
  timer_create(CLOCK_BOOTTIME, &sigevent, &timer);
}

void AlarmTestHarness::TearDown() {
  alarm_cleanup();
  timer_delete(timer);
  AllocationTestHarness::TearDown();
}

static bool set_wake_alarm(uint64_t delay_millis, bool, alarm_cb cb, void *data) {
  saved_callback = cb;
  saved_data = data;

  struct itimerspec wakeup_time;
  memset(&wakeup_time, 0, sizeof(wakeup_time));
  wakeup_time.it_value.tv_sec = (delay_millis / 1000);
  wakeup_time.it_value.tv_nsec = (delay_millis % 1000) * 1000000LL;
  timer_settime(timer, 0, &wakeup_time, NULL);
  return true;
}

static int acquire_wake_lock(const char *) {
  if (!current_harness->lock_count)
    current_harness->lock_count = 1;
  return BT_STATUS_SUCCESS;
}

static int release_wake_lock(const char *) {
  if (current_harness->lock_count)
    current_harness->lock_count = 0;
  return BT_STATUS_SUCCESS;
}

static bt_os_callouts_t stub = {
  sizeof(bt_os_callouts_t),
  set_wake_alarm,
  acquire_wake_lock,
  release_wake_lock,
};

bt_os_callouts_t *bt_os_callouts = &stub;

