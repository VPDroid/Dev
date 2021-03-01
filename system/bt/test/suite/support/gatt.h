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

extern const btgatt_interface_t *gatt_interface;

bool gatt_init();
int gatt_get_connection_id();
int gatt_get_client_interface();
int gatt_get_server_interface();
int gatt_get_service_handle();
int gatt_get_included_service_handle();
int gatt_get_characteristic_handle();
int gatt_get_descriptor_handle();
int gatt_get_status();
