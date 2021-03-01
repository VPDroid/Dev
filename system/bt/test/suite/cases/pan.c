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

static const int local_role = BTPAN_ROLE_PANU;
static const int remote_role = BTPAN_ROLE_PANNAP;

bool pan_enable() {
  int error;

  // PAN is enabled by default, wait for the first control state change
  // with default parameters set. We don't want to verify the result since
  // the implementation could have set any parameters.
  WAIT(pan_control_state_changed);

  // Call enable explicitly and verify that the parameters match what we just set.
  CALL_AND_WAIT(error = pan_interface->enable(local_role), pan_control_state_changed);
  TASSERT(error == BT_STATUS_SUCCESS, "Error enabling PAN: %d", error);
  TASSERT(pan_get_control_state() == BTPAN_STATE_ENABLED, "Control state is disabled.");
  TASSERT(pan_get_local_role() == local_role, "Unexpected local role: %d", pan_get_local_role());
  TASSERT(pan_get_error() == BT_STATUS_SUCCESS, "Error in control callback: %d", pan_get_error());

  return true;
}

bool pan_connect() {
  int error;

  if (!pan_enable()) {
    return false;
  }

  CALL_AND_WAIT(error = pan_interface->connect(&bt_remote_bdaddr, local_role, remote_role), pan_connection_state_changed);
  TASSERT(error == BT_STATUS_SUCCESS, "Error connecting to remote host: %d", error);
  TASSERT(pan_get_error() == BT_STATUS_SUCCESS, "Error connecting to BT device: %d", pan_get_error());
  TASSERT(pan_get_connection_state() == BTPAN_STATE_CONNECTING, "Invalid PAN state after connect: %d", pan_get_connection_state());
  TASSERT(pan_get_local_role() == local_role, "Incorrect local role: %d", pan_get_local_role());
  TASSERT(pan_get_remote_role() == remote_role, "Incorrect remote role: %d", pan_get_remote_role());

  WAIT(pan_connection_state_changed);
  TASSERT(pan_get_error() == BT_STATUS_SUCCESS, "Error connecting to BT device: %d", pan_get_error());
  TASSERT(pan_get_connection_state() == BTPAN_STATE_CONNECTED, "Invalid PAN state after connect: %d", pan_get_connection_state());
  TASSERT(pan_get_local_role() == local_role, "Incorrect local role: %d", pan_get_local_role());
  TASSERT(pan_get_remote_role() == remote_role, "Incorrect remote role: %d", pan_get_remote_role());

  return true;
}

bool pan_disconnect() {
  int error;

  if (!pan_connect()) {
    return false;
  }

  CALL_AND_WAIT(error = pan_interface->disconnect(&bt_remote_bdaddr), pan_connection_state_changed);
  TASSERT(error == BT_STATUS_SUCCESS, "Error disconnecting from remote host: %d", error);
  TASSERT(pan_get_error() == BT_STATUS_SUCCESS, "Error disconnecting from BT device: %d", pan_get_error());
  TASSERT(pan_get_connection_state() == BTPAN_STATE_DISCONNECTING, "Invalid PAN state after disconnect: %d", pan_get_connection_state());

  WAIT(pan_connection_state_changed);
  TASSERT(pan_get_error() == BT_STATUS_SUCCESS, "Error disconnecting from BT device: %d", pan_get_error());
  TASSERT(pan_get_connection_state() == BTPAN_STATE_DISCONNECTED, "Invalid PAN state after disconnect: %d", pan_get_connection_state());

  return true;
}
