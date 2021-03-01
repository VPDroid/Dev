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

#include "config.h"
#include "module.h"

static const char STACK_CONFIG_MODULE[] = "stack_config_module";

typedef struct {
  const char *(*get_btsnoop_log_path)(void);
  bool (*get_btsnoop_turned_on)(void);
  bool (*get_btsnoop_should_save_last)(void);
  bool (*get_trace_config_enabled)(void);
  config_t *(*get_all)(void);
} stack_config_t;

const stack_config_t *stack_config_get_interface();
