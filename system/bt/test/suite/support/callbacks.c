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
#include "support/callbacks.h"

// Bluetooth callbacks
void acl_state_changed(bt_status_t status, bt_bdaddr_t *remote_bd_addr, bt_acl_state_t state);
void adapter_properties(bt_status_t status, int num_properties, bt_property_t *properties);
void adapter_state_changed(bt_state_t state);
void bond_state_changed(bt_status_t status, bt_bdaddr_t *remote_bd_addr, bt_bond_state_t state);

void device_found(int num_properties, bt_property_t *properties);
void discovery_state_changed(bt_discovery_state_t state);
void remote_device_properties(bt_status_t status, bt_bdaddr_t *bd_addr, int num_properties, bt_property_t *properties);
void ssp_request(bt_bdaddr_t *remote_bd_addr, bt_bdname_t *bd_name, uint32_t cod, bt_ssp_variant_t pairing_variant, uint32_t pass_key);
void thread_evt(bt_cb_thread_evt evt);

// PAN callbacks
void pan_connection_state_changed(btpan_connection_state_t state, bt_status_t error, const bt_bdaddr_t *bd_addr, int local_role, int remote_role);
void pan_control_state_changed(btpan_control_state_t state, int local_role, bt_status_t error, const char *ifname);

// GATT client callbacks
void btgattc_register_app_cb(int status, int clientIf, bt_uuid_t *app_uuid);
void btgattc_scan_result_cb(bt_bdaddr_t* bda, int rssi, uint8_t* adv_data);
void btgattc_open_cb(int conn_id, int status, int clientIf, bt_bdaddr_t* bda);
void btgattc_close_cb(int conn_id, int status, int clientIf, bt_bdaddr_t* bda);
void btgattc_search_complete_cb(int conn_id, int status);
void btgattc_search_result_cb(int conn_id, btgatt_srvc_id_t *srvc_id);
void btgattc_get_characteristic_cb(int conn_id, int status, btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id, int char_prop);
void btgattc_get_descriptor_cb(int conn_id, int status, btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id, btgatt_gatt_id_t *descr_id);
void btgattc_get_included_service_cb(int conn_id, int status, btgatt_srvc_id_t *srvc_id, btgatt_srvc_id_t *incl_srvc_id);
void btgattc_register_for_notification_cb(int conn_id, int registered, int status, btgatt_srvc_id_t *srvc_id, btgatt_gatt_id_t *char_id);
void btgattc_notify_cb(int conn_id, btgatt_notify_params_t *p_data);
void btgattc_read_characteristic_cb(int conn_id, int status, btgatt_read_params_t *p_data);
void btgattc_write_characteristic_cb(int conn_id, int status, btgatt_write_params_t *p_data);
void btgattc_execute_write_cb(int conn_id, int status);
void btgattc_read_descriptor_cb(int conn_id, int status, btgatt_read_params_t *p_data);
void btgattc_write_descriptor_cb(int conn_id, int status, btgatt_write_params_t *p_data);
void btgattc_remote_rssi_cb(int client_if,bt_bdaddr_t* bda, int rssi, int status);
void btgattc_advertise_cb(int status, int client_if);

// GATT server callbacks
void btgatts_register_app_cb(int status, int server_if, bt_uuid_t *uuid);
void btgatts_connection_cb(int conn_id, int server_if, int connected, bt_bdaddr_t *bda);
void btgatts_service_added_cb(int status, int server_if, btgatt_srvc_id_t *srvc_id, int srvc_handle);
void btgatts_included_service_added_cb(int status, int server_if, int srvc_handle, int incl_srvc_handle);
void btgatts_characteristic_added_cb(int status, int server_if, bt_uuid_t *char_id, int srvc_handle, int char_handle);
void btgatts_descriptor_added_cb(int status, int server_if, bt_uuid_t *descr_id, int srvc_handle, int descr_handle);
void btgatts_service_started_cb(int status, int server_if, int srvc_handle);
void btgatts_service_stopped_cb(int status, int server_if, int srvc_handle);
void btgatts_service_deleted_cb(int status, int server_if, int srvc_handle);
void btgatts_request_read_cb(int conn_id, int trans_id, bt_bdaddr_t *bda, int attr_handle, int offset, bool is_long);
void btgatts_request_write_cb(int conn_id, int trans_id, bt_bdaddr_t *bda, int attr_handle, int offset, int length, bool need_rsp, bool is_prep, uint8_t* value);
void btgatts_request_exec_write_cb(int conn_id, int trans_id, bt_bdaddr_t *bda, int exec_write);
void btgatts_response_confirmation_cb(int status, int handle);

static struct {
  const char *name;
  sem_t semaphore;
} callback_data[] = {
  // Adapter callbacks
  { "adapter_state_changed" },
  { "adapter_properties" },
  { "remote_device_properties" },
  { "device_found" },
  { "discovery_state_changed" },
  {},
  { "ssp_request" },
  { "bond_state_changed" },
  { "acl_state_changed" },
  { "thread_evt" },
  {},
  {},

  // PAN callbacks
  { "pan_control_state_changed" },
  { "pan_connection_state_changed" },

  // GATT client callbacks
  { "btgattc_register_app_cb" },
  { "btgattc_scan_result_cb" },
  { "btgattc_open_cb" },
  { "btgattc_close_cb" },
  { "btgattc_search_complete_cb" },
  { "btgattc_search_result_cb" },
  { "btgattc_get_characteristic_cb" },
  { "btgattc_get_descriptor_cb" },
  { "btgattc_get_included_service_cb" },
  { "btgattc_register_for_notification_cb" },
  { "btgattc_notify_cb" },
  { "btgattc_read_characteristic_cb" },
  { "btgattc_write_characteristic_cb" },
  { "btgattc_execute_write_cb" },
  { "btgattc_read_descriptor_cb" },
  { "btgattc_write_descriptor_cb" },
  { "btgattc_remote_rssi_cb" },
  { "btgattc_advertise_cb" },
  {},
  {},
  {},
  {},
  {},
  {},

  // GATT server callbacks
  { "btgatts_register_app_cb" },
  { "btgatts_connection_cb" },
  { "btgatts_service_added_cb" },
  { "btgatts_included_service_added_cb" },
  { "btgatts_characteristic_added_cb" },
  { "btgatts_descriptor_added_cb" },
  { "btgatts_service_started_cb" },
  { "btgatts_service_stopped_cb" },
  { "btgatts_service_deleted_cb" },
  { "btgatts_request_read_cb" },
  { "btgatts_request_write_cb" },
  { "btgatts_request_exec_write_cb" },
  { "btgatts_response_confirmation_cb" },

};

static bt_callbacks_t bt_callbacks = {
  sizeof(bt_callbacks_t),
  adapter_state_changed,     // adapter_state_changed_callback
  adapter_properties,        // adapter_properties_callback
  remote_device_properties,  // remote_device_properties_callback
  device_found,              // device_found_callback
  discovery_state_changed,   // discovery_state_changed_callback
  NULL,                      // pin_request_callback
  ssp_request,               // ssp_request_callback
  bond_state_changed,        // bond_state_changed_callback
  acl_state_changed,         // acl_state_changed_callback
  thread_evt,                // callback_thread_event
  NULL,                      // dut_mode_recv_callback
  NULL,                      // le_test_mode_callback
  NULL,
};

static btpan_callbacks_t pan_callbacks = {
  sizeof(btpan_callbacks_t),
  pan_control_state_changed,     // btpan_control_state_callback
  pan_connection_state_changed,  // btpan_connection_state_callback
};

static const btgatt_client_callbacks_t gatt_client_callbacks = {
  btgattc_register_app_cb,
  btgattc_scan_result_cb,
  btgattc_open_cb,
  btgattc_close_cb,
  btgattc_search_complete_cb,
  btgattc_search_result_cb,
  btgattc_get_characteristic_cb,
  btgattc_get_descriptor_cb,
  btgattc_get_included_service_cb,
  btgattc_register_for_notification_cb,
  btgattc_notify_cb,
  btgattc_read_characteristic_cb,
  btgattc_write_characteristic_cb,
  btgattc_read_descriptor_cb,
  btgattc_write_descriptor_cb,
  btgattc_execute_write_cb,
  btgattc_remote_rssi_cb,
  btgattc_advertise_cb,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
};

static const btgatt_server_callbacks_t gatt_server_callbacks = {
  btgatts_register_app_cb,
  btgatts_connection_cb,
  btgatts_service_added_cb,
  btgatts_included_service_added_cb,
  btgatts_characteristic_added_cb,
  btgatts_descriptor_added_cb,
  btgatts_service_started_cb,
  btgatts_service_stopped_cb,
  btgatts_service_deleted_cb,
  btgatts_request_read_cb,
  btgatts_request_write_cb,
  btgatts_request_exec_write_cb,
  btgatts_response_confirmation_cb
};

static btgatt_callbacks_t gatt_callbacks = {
  sizeof(btgatt_callbacks_t),
  &gatt_client_callbacks,
  &gatt_server_callbacks
};

void callbacks_init() {
  for (size_t i = 0; i < ARRAY_SIZE(callback_data); ++i) {
    sem_init(&callback_data[i].semaphore, 0, 0);
  }
}

void callbacks_cleanup() {
  for (size_t i = 0; i < ARRAY_SIZE(callback_data); ++i) {
    sem_destroy(&callback_data[i].semaphore);
  }
}

bt_callbacks_t *callbacks_get_adapter_struct() {
  return &bt_callbacks;
}

btpan_callbacks_t *callbacks_get_pan_struct() {
  return &pan_callbacks;
}

btgatt_callbacks_t *callbacks_get_gatt_struct() {
  return &gatt_callbacks;
}

sem_t *callbacks_get_semaphore(const char *name) {
  for (size_t i = 0; i < ARRAY_SIZE(callback_data); ++i) {
    if (callback_data[i].name && !strcmp(name, callback_data[i].name)) {
      return &callback_data[i].semaphore;
    }
  }
  return NULL;
}
