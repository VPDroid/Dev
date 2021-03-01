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

#pragma once

#include <stdint.h>

typedef struct alarm_t alarm_t;
typedef uint64_t period_ms_t;

// Prototype for the callback function.
typedef void (*alarm_callback_t)(void *data);

// Creates a new alarm object. The returned object must be freed by calling
// |alarm_free|. Returns NULL on failure.
alarm_t *alarm_new(void);

// Frees an alarm object created by |alarm_new|. |alarm| may be NULL. If the
// alarm is pending, it will be cancelled. It is not safe to call |alarm_free|
// from inside the callback of |alarm|.
void alarm_free(alarm_t *alarm);

// Sets an alarm to fire |cb| after the given |deadline|. Note that |deadline| is the
// number of milliseconds relative to the current time. |data| is a context variable
// for the callback and may be NULL. |cb| will be called back in the context of an
// unspecified thread (i.e. it will not be called back in the same thread as the caller).
// |alarm| and |cb| may not be NULL.
void alarm_set(alarm_t *alarm, period_ms_t deadline, alarm_callback_t cb, void *data);

// Like alarm_set, except the alarm repeats every |period| ms until it is cancelled or
// freed or |alarm_set| is called to set it non-periodically.
void alarm_set_periodic(alarm_t *alarm, period_ms_t period, alarm_callback_t cb, void *data);

// This function cancels the |alarm| if it was previously set. When this call
// returns, the caller has a guarantee that the callback is not in progress and
// will not be called if it hasn't already been called. This function is idempotent.
// |alarm| may not be NULL.
void alarm_cancel(alarm_t *alarm);

// Figure out how much time until next expiration.
// Returns 0 if not armed. |alarm| may not be NULL.
// TODO: Remove this function once PM timers can be re-factored
period_ms_t alarm_get_remaining_ms(const alarm_t *alarm);

// Alarm-related state cleanup
void alarm_cleanup(void);
