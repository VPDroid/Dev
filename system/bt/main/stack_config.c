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

#define LOG_TAG "bt_stack_config"

#include <assert.h>

#include "osi/include/future.h"
#include "stack_config.h"
#include "osi/include/log.h"

const char *BTSNOOP_LOG_PATH_KEY = "BtSnoopFileName";
const char *BTSNOOP_TURNED_ON_KEY = "BtSnoopLogOutput";
const char *BTSNOOP_SHOULD_SAVE_LAST_KEY = "BtSnoopSaveLog";
const char *TRACE_CONFIG_ENABLED_KEY = "TraceConf";

static config_t *config;

// Module lifecycle functions

static future_t *init() {
  const char *path = "/etc/bluetooth/bt_stack.conf";
  assert(path != NULL);

  LOG_INFO("%s attempt to load stack conf from %s", __func__, path);

  config = config_new(path);
  if (!config) {
    LOG_INFO("%s file >%s< not found", __func__, path);
    return future_new_immediate(FUTURE_FAIL);
  }

  return future_new_immediate(FUTURE_SUCCESS);
}

static future_t *clean_up() {
  config_free(config);
  return future_new_immediate(FUTURE_SUCCESS);
}

const module_t stack_config_module = {
  .name = STACK_CONFIG_MODULE,
  .init = init,
  .start_up = NULL,
  .shut_down = NULL,
  .clean_up = clean_up,
  .dependencies = {
    NULL
  }
};

// Interface functions

static const char *get_btsnoop_log_path(void) {
  return config_get_string(config, CONFIG_DEFAULT_SECTION, BTSNOOP_LOG_PATH_KEY, "/data/misc/bluedroid/btsnoop_hci.log");
}

static bool get_btsnoop_turned_on(void) {
  return config_get_bool(config, CONFIG_DEFAULT_SECTION, BTSNOOP_TURNED_ON_KEY, false);
}

static bool get_btsnoop_should_save_last(void) {
  return config_get_bool(config, CONFIG_DEFAULT_SECTION, BTSNOOP_SHOULD_SAVE_LAST_KEY, false);
}

static bool get_trace_config_enabled(void) {
  return config_get_bool(config, CONFIG_DEFAULT_SECTION, TRACE_CONFIG_ENABLED_KEY, false);
}

static config_t *get_all(void) {
  return config;
}

const stack_config_t interface = {
  get_btsnoop_log_path,
  get_btsnoop_turned_on,
  get_btsnoop_should_save_last,
  get_trace_config_enabled,
  get_all
};

const stack_config_t *stack_config_get_interface() {
  return &interface;
}
