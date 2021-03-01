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
#include <stdint.h>

static const char COUNTER_MODULE[] = "counter_module";

typedef int64_t counter_data_t;

// Used to iterate across all counters.
typedef bool (*counter_iter_cb)(const char *name, counter_data_t val, void *context);

// Mutators.
void counter_set(const char *name, counter_data_t val);
void counter_add(const char *name, counter_data_t val);

// Iteration.
bool counter_foreach(counter_iter_cb, void *context);
