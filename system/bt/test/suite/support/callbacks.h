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

#include "base.h"

#include <errno.h>
#include <semaphore.h>
#include <unistd.h>

#define WAIT(callback) \
  do { \
    sem_t *semaphore = callbacks_get_semaphore(#callback); \
    TEMP_FAILURE_RETRY(sem_wait(semaphore)); \
  } while (0)

#define CALL_AND_WAIT(expression, callback) \
  do { \
    sem_t *semaphore = callbacks_get_semaphore(#callback); \
    while (!sem_trywait(semaphore)); \
    expression; \
    TEMP_FAILURE_RETRY(sem_wait(semaphore)); \
  } while(0)

// To be called from every exit point of the callback. This macro
// takes 0 or 1 arguments, the return value of the callback.
#define CALLBACK_RET(...) do { \
    sem_t *semaphore = callbacks_get_semaphore(__func__); \
    sem_post(semaphore); \
    return __VA_ARGS__; \
  } while (0)

void callbacks_init();
void callbacks_cleanup();

bt_callbacks_t *callbacks_get_adapter_struct();
btpan_callbacks_t *callbacks_get_pan_struct();
btgatt_callbacks_t *callbacks_get_gatt_struct();
sem_t *callbacks_get_semaphore(const char *name);
