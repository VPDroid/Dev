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

#define LOG_TAG "bt_hci_mct"

#include <assert.h>
#include <errno.h>
#include <string.h>

#include "bt_vendor_lib.h"
#include "osi/include/eager_reader.h"
#include "hci_hal.h"
#include "osi/include/osi.h"
#include "osi/include/log.h"
#include "osi/include/reactor.h"
#include "vendor.h"

#define HCI_HAL_SERIAL_BUFFER_SIZE 1026

// Our interface and modules we import
static const hci_hal_t interface;
static const hci_hal_callbacks_t *callbacks;
static const vendor_t *vendor;

static thread_t *thread; // Not owned by us

static int uart_fds[CH_MAX];
static eager_reader_t *event_stream;
static eager_reader_t *acl_stream;

static uint16_t transmit_data_on(int fd, uint8_t *data, uint16_t length);
static void event_event_stream_has_bytes(eager_reader_t *reader, void *context);
static void event_acl_stream_has_bytes(eager_reader_t *reader, void *context);

// Interface functions

static bool hal_init(const hci_hal_callbacks_t *upper_callbacks, thread_t *upper_thread) {
  assert(upper_callbacks != NULL);
  assert(upper_thread != NULL);

  callbacks = upper_callbacks;
  thread = upper_thread;
  return true;
}

static bool hal_open() {
  LOG_INFO("%s", __func__);
  // TODO(zachoverflow): close if already open / or don't reopen (maybe at the hci layer level)

  int number_of_ports = vendor->send_command(VENDOR_OPEN_USERIAL, &uart_fds);

  if (number_of_ports != 2 && number_of_ports != 4) {
    LOG_ERROR("%s opened the wrong number of ports: got %d, expected 2 or 4.", __func__, number_of_ports);
    goto error;
  }

  LOG_INFO("%s got uart fds: CMD=%d, EVT=%d, ACL_OUT=%d, ACL_IN=%d",
      __func__, uart_fds[CH_CMD], uart_fds[CH_EVT], uart_fds[CH_ACL_OUT], uart_fds[CH_ACL_IN]);

  if (uart_fds[CH_CMD] == INVALID_FD) {
    LOG_ERROR("%s unable to open the command uart serial port.", __func__);
    goto error;
  }

  if (uart_fds[CH_EVT] == INVALID_FD) {
    LOG_ERROR("%s unable to open the event uart serial port.", __func__);
    goto error;
  }

  if (uart_fds[CH_ACL_OUT] == INVALID_FD) {
    LOG_ERROR("%s unable to open the acl-out uart serial port.", __func__);
    goto error;
  }

  if (uart_fds[CH_ACL_IN] == INVALID_FD) {
    LOG_ERROR("%s unable to open the acl-in uart serial port.", __func__);
    goto error;
  }

  event_stream = eager_reader_new(uart_fds[CH_EVT], &allocator_malloc, HCI_HAL_SERIAL_BUFFER_SIZE, SIZE_MAX, "hci_mct");
  if (!event_stream) {
    LOG_ERROR("%s unable to create eager reader for the event uart serial port.", __func__);
    goto error;
  }

  acl_stream = eager_reader_new(uart_fds[CH_ACL_IN], &allocator_malloc, HCI_HAL_SERIAL_BUFFER_SIZE, SIZE_MAX, "hci_mct");
  if (!event_stream) {
    LOG_ERROR("%s unable to create eager reader for the acl-in uart serial port.", __func__);
    goto error;
  }

  eager_reader_register(event_stream, thread_get_reactor(thread), event_event_stream_has_bytes, NULL);
  eager_reader_register(acl_stream, thread_get_reactor(thread), event_acl_stream_has_bytes, NULL);

  return true;

error:;
  interface.close();
  return false;
}

static void hal_close() {
  LOG_INFO("%s", __func__);

  eager_reader_free(event_stream);
  eager_reader_free(acl_stream);
  vendor->send_command(VENDOR_CLOSE_USERIAL, NULL);

  for (int i = 0; i < CH_MAX; i++)
    uart_fds[i] = INVALID_FD;
}

static size_t read_data(serial_data_type_t type, uint8_t *buffer, size_t max_size, bool block) {
  if (type == DATA_TYPE_ACL) {
    return eager_reader_read(acl_stream, buffer, max_size, block);
  } else if (type == DATA_TYPE_EVENT) {
    return eager_reader_read(event_stream, buffer, max_size, block);
  }

  LOG_ERROR("%s invalid data type: %d", __func__, type);
  return 0;
}

static void packet_finished(UNUSED_ATTR serial_data_type_t type) {
  // not needed by this protocol
}

static uint16_t transmit_data(serial_data_type_t type, uint8_t *data, uint16_t length) {
  if (type == DATA_TYPE_ACL) {
    return transmit_data_on(uart_fds[CH_ACL_OUT], data, length);
  } else if (type == DATA_TYPE_COMMAND) {
    return transmit_data_on(uart_fds[CH_CMD], data, length);
  }

  LOG_ERROR("%s invalid data type: %d", __func__, type);
  return 0;
}

// Internal functions

static uint16_t transmit_data_on(int fd, uint8_t *data, uint16_t length) {
  assert(data != NULL);
  assert(length > 0);

  uint16_t transmitted_length = 0;
  while (length > 0) {
    ssize_t ret = TEMP_FAILURE_RETRY(write(fd, data + transmitted_length, length));
    switch (ret) {
      case -1:
        LOG_ERROR("In %s, error writing to the serial port with fd %d: %s", __func__, fd, strerror(errno));
        return transmitted_length;
      case 0:
        // If we wrote nothing, don't loop more because we
        // can't go to infinity or beyond
        return transmitted_length;
      default:
        transmitted_length += ret;
        length -= ret;
        break;
    }
  }

  return transmitted_length;
}

static void event_event_stream_has_bytes(UNUSED_ATTR eager_reader_t *reader, UNUSED_ATTR void *context) {
  callbacks->data_ready(DATA_TYPE_EVENT);
}

static void event_acl_stream_has_bytes(UNUSED_ATTR eager_reader_t *reader, UNUSED_ATTR void *context) {
  // No real concept of incoming SCO typed data, just ACL
  callbacks->data_ready(DATA_TYPE_ACL);
}

static const hci_hal_t interface = {
  hal_init,

  hal_open,
  hal_close,

  read_data,
  packet_finished,
  transmit_data,
};

const hci_hal_t *hci_hal_mct_get_interface() {
  vendor = vendor_get_interface();
  return &interface;
}

const hci_hal_t *hci_hal_mct_get_test_interface(vendor_t *vendor_interface) {
  vendor = vendor_interface;
  return &interface;
}
