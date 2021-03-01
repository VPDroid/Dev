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

#include <sys/socket.h>
#include <unistd.h>

#include "base.h"
#include "support/callbacks.h"
#include "support/rfcomm.h"

static const bt_uuid_t HFP_AG_UUID = {{ 0x00, 0x00, 0x11, 0x1F, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB }};
static const char HANDSHAKE_COMMAND[] = "AT+BRSF=29\r";
static const char HANDSHAKE_RESPONSE[] = "OK\r\n";

static bool handshake(int fd) {
  TASSERT(write(fd, HANDSHAKE_COMMAND, sizeof(HANDSHAKE_COMMAND)) == sizeof(HANDSHAKE_COMMAND), "Unable to send HFP handshake.");

  char response[256] = { 0 };
  size_t total = 0;
  while (!strstr(response, HANDSHAKE_RESPONSE) && total < sizeof(response)) {
    ssize_t len = read(fd, response + total, sizeof(response) - total);
    TASSERT(len != -1 && len != 0, "Unable to read complete response from socket.");
    total += len;
  }
  TASSERT(strstr(response, HANDSHAKE_RESPONSE) != NULL, "No valid response from HFP audio gateway.");
  return true;
}

bool rfcomm_connect() {
  int fd;
  int error;

  error = socket_interface->connect(&bt_remote_bdaddr, BTSOCK_RFCOMM, (const uint8_t *)&HFP_AG_UUID, 0, &fd, 0);
  TASSERT(error == BT_STATUS_SUCCESS, "Error creating RFCOMM socket: %d", error);
  TASSERT(fd != -1, "Error creating RFCOMM socket: invalid fd");

  int channel;
  sock_connect_signal_t signal;
  TASSERT(read(fd, &channel, sizeof(channel)) == sizeof(channel), "Channel not read from RFCOMM socket.");
  TASSERT(read(fd, &signal, sizeof(signal)) == sizeof(signal), "Connection signal not read from RFCOMM socket.");

  TASSERT(!memcmp(&signal.bd_addr, &bt_remote_bdaddr, sizeof(bt_bdaddr_t)), "Connected to a different bdaddr than expected.");
  TASSERT(channel == signal.channel, "Inconsistent channels returned: %d and %d", channel, signal.channel);

  if (!handshake(fd))
    return false;

  close(fd);
  return true;
}

bool rfcomm_repeated_connect() {
  static const int max_iterations = 128;

  for (int i = 0; i < max_iterations; ++i) {
    TASSERT(rfcomm_connect(), "Connection failed on attempt %d/%d", i, max_iterations);
  }

  return true;
}
