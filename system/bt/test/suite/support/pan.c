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
#include "support/pan.h"

const btpan_interface_t *pan_interface;

static btpan_control_state_t pan_control_state;
static btpan_connection_state_t pan_connection_state;
static int pan_local_role;
static int pan_remote_role;
static bt_status_t pan_error;
static char *pan_ifname;

bool pan_init() {
  pan_interface = bt_interface->get_profile_interface(BT_PROFILE_PAN_ID);
  return pan_interface->init(callbacks_get_pan_struct()) == BT_STATUS_SUCCESS;
}

btpan_control_state_t pan_get_control_state() {
  return pan_control_state;
}

btpan_connection_state_t pan_get_connection_state() {
  return pan_connection_state;
}

int pan_get_local_role() {
  return pan_local_role;
}

int pan_get_remote_role() {
  return pan_remote_role;
}

bt_status_t pan_get_error() {
  return pan_error;
}

// callback
void pan_control_state_changed(btpan_control_state_t state, bt_status_t error, int local_role, const char *ifname) {
  free(pan_ifname);

  pan_control_state = state;
  pan_local_role = local_role;
  pan_error = error;
  pan_ifname = strdup(ifname);

  CALLBACK_RET();
}

// callback
void pan_connection_state_changed(btpan_connection_state_t state, bt_status_t error, const bt_bdaddr_t *bd_addr, int local_role, int remote_role) {
  pan_connection_state = state;
  pan_error = error;
  pan_local_role = local_role;
  pan_remote_role = remote_role;
  CALLBACK_RET();
}
