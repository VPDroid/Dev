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

#include <assert.h>

#include "osi/include/allocator.h"
#include "osi/include/osi.h"
#include "osi/include/non_repeating_timer.h"

struct non_repeating_timer_t {
  alarm_t *alarm;
  period_ms_t duration;
  alarm_callback_t callback;
  void *data;
};

non_repeating_timer_t *non_repeating_timer_new(period_ms_t duration, alarm_callback_t cb, void *data) {
  assert(cb != NULL);

  non_repeating_timer_t *ret = osi_calloc(sizeof(non_repeating_timer_t));

  ret->alarm = alarm_new();
  if (!ret->alarm)
    goto error;

  ret->duration = duration;
  ret->callback = cb;
  ret->data = data;

  return ret;
error:;
  non_repeating_timer_free(ret);
  return NULL;
}

void non_repeating_timer_free(non_repeating_timer_t *timer) {
  if (!timer)
    return;

  alarm_free(timer->alarm);
  osi_free(timer);
}

void non_repeating_timer_restart(non_repeating_timer_t *timer) {
  non_repeating_timer_restart_if(timer, true);
}

void non_repeating_timer_restart_if(non_repeating_timer_t *timer, bool condition) {
  assert(timer != NULL);
  if (condition)
    alarm_set(timer->alarm, timer->duration, timer->callback, timer->data);
  else
    non_repeating_timer_cancel(timer);
}

void non_repeating_timer_cancel(non_repeating_timer_t *timer) {
  assert(timer != NULL);
  alarm_cancel(timer->alarm);
}
