/******************************************************************************
 *
 *  Copyright (C) 2015 Google Inc.
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

#include <assert.h>
#include <resolv.h>
#include <zlib.h>

#include "btif/include/btif_debug.h"
#include "btif/include/btif_debug_btsnoop.h"
#include "hci/include/btsnoop_mem.h"
#include "include/bt_target.h"
#include "osi/include/ringbuffer.h"

#define REDUCE_HCI_TYPE_TO_SIGNIFICANT_BITS(type) (type >> 8)

// Total btsnoop memory log buffer size
#ifndef BTSNOOP_MEM_BUFFER_SIZE
static const size_t BTSNOOP_MEM_BUFFER_SIZE = (128 * 1024);
#endif

// Block size for copying buffers (for compression/encoding etc.)
static const size_t BLOCK_SIZE = 16384;

// Maximum line length in bugreport (should be multiple of 4 for base64 output)
static const uint8_t MAX_LINE_LENGTH = 128;

static ringbuffer_t *buffer = NULL;
static uint64_t last_timestamp_ms = 0;

static void btsnoop_cb(const uint16_t type, const uint8_t *data, const size_t length) {
  btsnooz_header_t header;

  // Make room in the ring buffer

  while (ringbuffer_available(buffer) < (length + sizeof(btsnooz_header_t))) {
    ringbuffer_pop(buffer, (uint8_t *)&header, sizeof(btsnooz_header_t));
    ringbuffer_delete(buffer, header.length - 1);
  }

  // Insert data

  const uint64_t now = btif_debug_ts();

  header.type = REDUCE_HCI_TYPE_TO_SIGNIFICANT_BITS(type);
  header.length = length;
  header.delta_time_ms = last_timestamp_ms ? now - last_timestamp_ms : 0;
  last_timestamp_ms = now;

  ringbuffer_insert(buffer, (uint8_t *)&header, sizeof(btsnooz_header_t));
  ringbuffer_insert(buffer, data, length - 1);
}

static bool btsnoop_compress(ringbuffer_t *rb_dst, ringbuffer_t *rb_src) {
  assert(rb_dst != NULL);
  assert(rb_src != NULL);

  z_stream zs = {0};
  if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK)
    return false;

  bool rc = true;
  uint8_t block_src[BLOCK_SIZE];
  uint8_t block_dst[BLOCK_SIZE];

  while (ringbuffer_size(rb_src) > 0) {
    zs.avail_in = ringbuffer_pop(rb_src, block_src, BLOCK_SIZE);
    zs.next_in = block_src;

    do {
      zs.avail_out = BLOCK_SIZE;
      zs.next_out = block_dst;

      int err = deflate(&zs, ringbuffer_size(rb_src) == 0 ? Z_FINISH : Z_NO_FLUSH);
      if (err == Z_STREAM_ERROR) {
        rc = false;
        break;
      }

      const size_t length = BLOCK_SIZE - zs.avail_out;
      ringbuffer_insert(rb_dst, block_dst, length);
    } while (zs.avail_out == 0);
  }

  deflateEnd(&zs);
  return rc;
}

void btif_debug_btsnoop_init(void) {
  if (buffer == NULL)
    buffer = ringbuffer_init(BTSNOOP_MEM_BUFFER_SIZE);
  btsnoop_mem_set_callback(btsnoop_cb);
}

void btif_debug_btsnoop_dump(int fd) {
  dprintf(fd, "\n--- BEGIN:BTSNOOP_LOG_SUMMARY (%zu bytes in) ---\n", ringbuffer_size(buffer));

  ringbuffer_t *ringbuffer = ringbuffer_init(BTSNOOP_MEM_BUFFER_SIZE);
  if (ringbuffer == NULL) {
    dprintf(fd, "%s Unable to allocate memory for compression", __func__);
    return;
  }

  // Prepend preamble

  btsnooz_preamble_t preamble;
  preamble.version = BTSNOOZ_CURRENT_VERSION;
  preamble.last_timestamp_ms = last_timestamp_ms;
  ringbuffer_insert(ringbuffer, (uint8_t *)&preamble, sizeof(btsnooz_preamble_t));

  // Compress data

  bool rc = btsnoop_compress(ringbuffer, buffer);
  if (rc == false) {
    dprintf(fd, "%s Log compression failed", __func__);
    goto error;
  }

  // Base64 encode & output

  uint8_t b64_in[3] = {0};
  char b64_out[5] = {0};

  size_t i = sizeof(btsnooz_preamble_t);
  while (ringbuffer_size(ringbuffer) > 0) {
    size_t read = ringbuffer_pop(ringbuffer, b64_in, 3);
    if (i > 0 && i % MAX_LINE_LENGTH == 0)
      dprintf(fd, "\n");
    i += b64_ntop(b64_in, read, b64_out, 5);
    dprintf(fd, "%s", b64_out);
  }

  dprintf(fd, "\n--- END:BTSNOOP_LOG_SUMMARY (%zu bytes out) ---\n", i);

error:
  ringbuffer_free(ringbuffer);
}
