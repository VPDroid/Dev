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

#include <stdbool.h>

#include "alarm.h"

typedef struct non_repeating_timer_t non_repeating_timer_t;

// Creates a new non repeating timer of |duration| with callback |cb|. |cb| will be
// called back in the context of an upspecified thread, and may not be NULL. |data|
// is a context variable for the callback and may be NULL. The returned object must
// be freed by calling |non_repeating_timer_free|. Returns NULL on failure.
non_repeating_timer_t *non_repeating_timer_new(period_ms_t duration, alarm_callback_t cb, void *data);

// Frees a non repeating timer created by |non_repeating_timer_new|. |timer| may be
// NULL. If the timer is currently running, it will be cancelled. It is not safe to
// call |non_repeating_timer_free| from inside the callback of a non repeating timer.
void non_repeating_timer_free(non_repeating_timer_t *timer);

// Restarts the non repeating timer. If it is currently running, the timer is reset.
// |timer| may not be NULL.
void non_repeating_timer_restart(non_repeating_timer_t *timer);

// Restarts the non repeating timer if |condition| is true, otherwise ensures it is
// not running. |timer| may not be NULL.
void non_repeating_timer_restart_if(non_repeating_timer_t *timer, bool condition);

// Cancels the non repeating timer if it is currently running. All the semantics of
// |alarm_cancel| apply here. |timer| may not be NULL.
void non_repeating_timer_cancel(non_repeating_timer_t *timer);
