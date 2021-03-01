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

#include "base.h"
#include "cases/cases.h"

TEST_CASE_DECL(adapter_enable_disable);
TEST_CASE_DECL(adapter_repeated_enable_disable);
TEST_CASE_DECL(adapter_set_name);
TEST_CASE_DECL(adapter_get_name);
TEST_CASE_DECL(adapter_start_discovery);
TEST_CASE_DECL(adapter_cancel_discovery);

TEST_CASE_DECL(rfcomm_connect);
TEST_CASE_DECL(rfcomm_repeated_connect);

TEST_CASE_DECL(pan_enable);
TEST_CASE_DECL(pan_connect);
TEST_CASE_DECL(pan_disconnect);
TEST_CASE_DECL(pan_quick_reconnect);

TEST_CASE_DECL(gatt_client_register);
TEST_CASE_DECL(gatt_client_scan);
TEST_CASE_DECL(gatt_client_advertise);
TEST_CASE_DECL(gatt_server_register);
TEST_CASE_DECL(gatt_server_build);

// These are run with the Bluetooth adapter disabled.
const test_case_t sanity_suite[] = {
  TEST_CASE(adapter_enable_disable),
  TEST_CASE(adapter_repeated_enable_disable)
};

// The normal test suite is run with the adapter enabled.
const test_case_t test_suite[] = {
  TEST_CASE(adapter_set_name),
  TEST_CASE(adapter_get_name),
  TEST_CASE(adapter_start_discovery),
  TEST_CASE(adapter_cancel_discovery),

  TEST_CASE(rfcomm_connect),
  TEST_CASE(rfcomm_repeated_connect),

  TEST_CASE(pan_enable),
  TEST_CASE(pan_connect),
  TEST_CASE(pan_disconnect),

  TEST_CASE(gatt_client_register),
  TEST_CASE(gatt_client_scan),
  TEST_CASE(gatt_client_advertise),
  TEST_CASE(gatt_server_register),
  TEST_CASE(gatt_server_build)
};

const size_t sanity_suite_size = ARRAY_SIZE(sanity_suite);
const size_t test_suite_size = ARRAY_SIZE(test_suite);
