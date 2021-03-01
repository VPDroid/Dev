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

#include <signal.h>
#include <time.h>

#include "base.h"
#include "support/hal.h"
#include "osi/include/hash_functions.h"
#include "osi/include/hash_map.h"

#define TIMER_BUCKET_COUNT 4

static bool set_wake_alarm(uint64_t delay_millis, bool should_wake, alarm_cb cb, void *data);
static int acquire_wake_lock(const char *lock_name);
static int release_wake_lock(const char *lock_name);

static const bluetooth_device_t *bt_device;

static bt_os_callouts_t callouts = {
  sizeof(bt_os_callouts_t),
  set_wake_alarm,
  acquire_wake_lock,
  release_wake_lock,
};

bool hal_open(bt_callbacks_t *callbacks) {
  hw_module_t *module;
  if (hw_get_module(BT_STACK_MODULE_ID, (hw_module_t const **)&module)) {
    return false;
  }

  hw_device_t *device;
  if (module->methods->open(module, BT_STACK_MODULE_ID, &device)) {
    return false;
  }

  bt_device = (bluetooth_device_t *)device;
  bt_interface = bt_device->get_bluetooth_interface();
  if (!bt_interface) {
    bt_device->common.close((hw_device_t *)&bt_device->common);
    bt_device = NULL;
    return false;
  }

  bool success = (bt_interface->init(callbacks) == BT_STATUS_SUCCESS);
  success = success && (bt_interface->set_os_callouts(&callouts) == BT_STATUS_SUCCESS);
  return success;
}

void hal_close() {
  if (bt_interface) {
    bt_interface->cleanup();
    bt_interface = NULL;
  }

  if (bt_device) {
    bt_device->common.close((hw_device_t *)&bt_device->common);
    bt_device = NULL;
  }
}

static bool set_wake_alarm(uint64_t delay_millis, bool should_wake, alarm_cb cb, void *data) {
  static hash_map_t *timers;

  if (!timers) {
    timers = hash_map_new(TIMER_BUCKET_COUNT, hash_function_pointer, NULL, NULL, NULL);
  }

  timer_t *timer = hash_map_get(timers, cb);
  if (!timer) {
    timer = malloc(sizeof(timer_t));
    hash_map_set(timers, cb, timer);

    struct sigevent sigevent;
    memset(&sigevent, 0, sizeof(sigevent));
    sigevent.sigev_notify = SIGEV_THREAD;
    sigevent.sigev_notify_function = (void (*)(union sigval))cb;
    sigevent.sigev_value.sival_ptr = data;
    timer_create(CLOCK_MONOTONIC, &sigevent, timer);
  }

  struct itimerspec new_value;
  new_value.it_value.tv_sec = delay_millis / 1000;
  new_value.it_value.tv_nsec = (delay_millis % 1000) * 1000 * 1000;
  new_value.it_interval.tv_sec = 0;
  new_value.it_interval.tv_nsec = 0;
  timer_settime(*timer, 0, &new_value, NULL);

  return true;
}

static int acquire_wake_lock(const char *lock_name) {
  return BT_STATUS_SUCCESS;
}

static int release_wake_lock(const char *lock_name) {
  return BT_STATUS_SUCCESS;
}
