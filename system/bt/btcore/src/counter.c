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

#define LOG_TAG "bt_core_counter"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/eventfd.h>

#include "osi/include/allocator.h"
#include "osi/include/atomic.h"
#include "btcore/include/counter.h"
#include "osi/include/hash_map.h"
#include "osi/include/list.h"
#include "btcore/include/module.h"
#include "osi/include/osi.h"
#include "osi/include/hash_functions.h"
#include "osi/include/log.h"
#include "osi/include/socket.h"
#include "osi/include/thread.h"

typedef int (*handler_t)(socket_t * socket);

typedef struct counter_t {
  atomic_s64_t val;
} counter_t;

typedef struct hash_element_t {
  const char *key;
  counter_t *val;
} hash_element_t;

typedef struct counter_data_cb_t {
  counter_iter_cb counter_iter_cb;
  void *user_context;
} counter_data_cb_t;

typedef struct {
  socket_t *socket;
  uint8_t buffer[256];
  size_t buffer_size;
} client_t;

typedef struct {
  const char *name;
  const char *help;
  handler_t handler;
} command_t;

// Counter core
static hash_map_t *hash_map_counter_;
static pthread_mutex_t hash_map_lock_;
static int counter_cnt_;

// Counter port access
static socket_t *listen_socket_;
static thread_t *thread_;
static list_t *clients_;

static void accept_ready(socket_t *socket, void *context);
static void read_ready(socket_t *socket, void *context);
static void client_free(void *ptr);
static const command_t *find_command(const char *name);
static void output(socket_t *socket, const char* format, ...);

// Commands
static int help(socket_t *socket);
static int show(socket_t *socket);
static int set(socket_t *socket);
static int quit(socket_t *socket);

static const command_t commands[] = {
  { "help", "<command> - show help text for <command>", help},
  { "quit", "<command> - Quit and exit", quit},
  { "set", "<counter> - Set something", set},
  { "show", "<counter> - Show counters", show},
};

static counter_t *counter_new_(counter_data_t initial_val);
static void counter_free_(counter_t *counter);

static hash_element_t *hash_element_new_(void);
// NOTE: The parameter datatype is void in order to satisfy the hash
// data free function signature
static void hash_element_free_(void *data);

static struct counter_t *name_to_counter_(const char *name);
static bool counter_foreach_cb_(hash_map_entry_t *hash_map_entry, void *context);

static bool counter_socket_open(void);
static void counter_socket_close(void);

static const int COUNTER_NUM_BUCKETS = 53;

// TODO(cmanton) Friendly interface, but may remove for automation
const char *WELCOME = "Welcome to counters\n";
const char *PROMPT = "\n> ";
const char *GOODBYE = "Quitting... Bye !!";

// TODO(cmanton) Develop port strategy; or multiplex all bt across single port
static const port_t LISTEN_PORT = 8879;

static future_t *counter_init(void) {
  assert(hash_map_counter_ == NULL);
  pthread_mutex_init(&hash_map_lock_, NULL);
  hash_map_counter_ = hash_map_new(COUNTER_NUM_BUCKETS, hash_function_string,
      NULL, hash_element_free_, NULL);
  if (hash_map_counter_ == NULL) {
    LOG_ERROR("%s unable to allocate resources", __func__);
    return future_new_immediate(FUTURE_FAIL);
  }

  if (!counter_socket_open()) {
    LOG_ERROR("%s unable to open counter port", __func__);
    return future_new_immediate(FUTURE_FAIL);
  }
  return future_new_immediate(FUTURE_SUCCESS);
}

static future_t *counter_clean_up(void) {
  counter_socket_close();
  hash_map_free(hash_map_counter_);
  pthread_mutex_destroy(&hash_map_lock_);
  hash_map_counter_ = NULL;
  return future_new_immediate(FUTURE_SUCCESS);
}

module_t counter_module = {
  .name = COUNTER_MODULE,
  .init = counter_init,
  .start_up = NULL,
  .shut_down = NULL,
  .clean_up = counter_clean_up,
  .dependencies = {NULL},
};

void counter_set(const char *name, counter_data_t val) {
  assert(name != NULL);
  counter_t *counter = name_to_counter_(name);
  if (counter)
    atomic_store_s64(&counter->val, val);
}

void counter_add(const char *name, counter_data_t val) {
  assert(name != NULL);
  counter_t *counter = name_to_counter_(name);
  if (counter) {
    if (val == 1)
      atomic_inc_prefix_s64(&counter->val);
    else
      atomic_add_s64(&counter->val, val);
  }
}

bool counter_foreach(counter_iter_cb cb, void *context) {
  assert(cb != NULL);
  counter_data_cb_t counter_cb_data = {
    cb,
    context
  };

  hash_map_foreach(hash_map_counter_, counter_foreach_cb_, &counter_cb_data);
  return true;
}

static counter_t *counter_new_(counter_data_t initial_val) {
  counter_t *counter = (counter_t *)osi_calloc(sizeof(counter_t));
  if (!counter) {
    return NULL;
  }
  atomic_store_s64(&counter->val, initial_val);
  return counter;
}

static void counter_free_(counter_t *counter) {
  osi_free(counter);
}

static hash_element_t *hash_element_new_(void) {
  return (hash_element_t *)osi_calloc(sizeof(hash_element_t));
}

static void hash_element_free_(void *data) {
  hash_element_t *hash_element = (hash_element_t *)data;
  // We don't own the key
  counter_free_(hash_element->val);
  osi_free(hash_element);
}

// Returns a counter from the |hash_map_counter_|.  Creates
// a new one if not found and inserts into |hash_map_counter_|.
// Returns NULL upon memory allocation failure.
static counter_t *name_to_counter_(const char *name) {
  assert(hash_map_counter_ != NULL);
  if (hash_map_has_key(hash_map_counter_, name))
    return (counter_t *)hash_map_get(hash_map_counter_, name);

  pthread_mutex_lock(&hash_map_lock_);
  // On the uncommon path double check to make sure that another thread has
  // not already created this counter
  counter_t *counter = (counter_t *)hash_map_get(hash_map_counter_, name);
  if (counter)
    goto exit;

  counter = counter_new_(0);
  if (!counter) {
    LOG_ERROR("%s unable to create new counter name:%s", __func__, name);
    goto exit;
  }

  hash_element_t *element = hash_element_new_();
  if (!element) {
    LOG_ERROR("%s unable to create counter element name:%s", __func__, name);
    counter_free_(counter);
    counter = NULL;
    goto exit;
  }

  element->key = name;
  element->val = counter;
  if (!hash_map_set(hash_map_counter_, name, counter)) {
    LOG_ERROR("%s unable to set new counter into hash map name:%s", __func__, name);
    hash_element_free_(element);
    counter_free_(counter);
    counter = NULL;
  }

 exit:;
  pthread_mutex_unlock(&hash_map_lock_);
  return counter;
}

static bool counter_foreach_cb_(hash_map_entry_t *hash_map_entry, void *context) {
  assert(hash_map_entry != NULL);
  const char *key = (const char *)hash_map_entry->key;
  counter_data_t data = *(counter_data_t *)hash_map_entry->data;
  counter_data_cb_t *counter_cb_data = (counter_data_cb_t *)context;
  counter_cb_data->counter_iter_cb(key, data, counter_cb_data->user_context);
  return true;
}

static bool counter_socket_open(void) {
#if (!defined(BT_NET_DEBUG) || (BT_NET_DEBUG != TRUE))
  return true;          // Disable using network sockets for security reasons
#endif

  assert(listen_socket_ == NULL);
  assert(thread_ == NULL);
  assert(clients_ == NULL);

  clients_ = list_new(client_free);
  if (!clients_) {
    LOG_ERROR("%s unable to create counter clients list", __func__);
    goto error;
  }

  thread_ = thread_new("counter_socket");
  if (!thread_) {
    LOG_ERROR("%s unable to create counter thread", __func__);
    goto error;
  }

  listen_socket_ = socket_new();
  if (!listen_socket_) {
    LOG_ERROR("%s unable to create listen socket", __func__);
    goto error;
  }

  if (!socket_listen(listen_socket_, LISTEN_PORT)) {
    LOG_ERROR("%s unable to setup listen socket", __func__);
    goto error;
  }

  LOG_INFO("%s opened counter server socket", __func__);
  socket_register(listen_socket_, thread_get_reactor(thread_), NULL, accept_ready, NULL);
  return true;

error:;
  counter_socket_close();
  return false;
}

static void counter_socket_close(void) {
#if (!defined(BT_NET_DEBUG) || (BT_NET_DEBUG != TRUE))
  return;               // Disable using network sockets for security reasons
#endif

  socket_free(listen_socket_);
  thread_free(thread_);
  list_free(clients_);

  listen_socket_ = NULL;
  thread_ = NULL;
  clients_ = NULL;

  LOG_INFO("%s closed counter server socket", __func__);
}

static bool monitor_counter_iter_cb(const char *name, counter_data_t val, void *context) {
  socket_t *socket = (socket_t *)context;
  output(socket, "counter:%s val:%lld\n", name, val);
  return true;
}

static void client_free(void *ptr) {
  if (!ptr)
    return;

  client_t *client = (client_t *)ptr;
  socket_free(client->socket);
  osi_free(client);
}

static void accept_ready(socket_t *socket, UNUSED_ATTR void *context) {
  assert(socket != NULL);
  assert(socket == listen_socket_);

  LOG_INFO("%s accepted OSI monitor socket", __func__);
  socket = socket_accept(socket);
  if (!socket)
    return;

  client_t *client = (client_t *)osi_calloc(sizeof(client_t));
  if (!client) {
    LOG_ERROR("%s unable to allocate memory for client", __func__);
    socket_free(socket);
    return;
  }

  client->socket = socket;

  if (!list_append(clients_, client)) {
    LOG_ERROR("%s unable to add client to list", __func__);
    client_free(client);
    return;
  }

  socket_register(socket, thread_get_reactor(thread_), client, read_ready, NULL);

  output(socket, WELCOME);
  output(socket, PROMPT);
}

static void read_ready(socket_t *socket, void *context) {
  assert(socket != NULL);

  client_t *client = (client_t *)context;

  ssize_t ret = socket_read(socket, client->buffer + client->buffer_size, sizeof(client->buffer) - client->buffer_size);
  if (ret == 0 || (ret == -1 && ret != EWOULDBLOCK && ret != EAGAIN)) {
    list_remove(clients_, client);
    return;
  }

  // Replace newline with end of string termination
  // TODO(cmanton) Need proper semantics
  for (size_t i = ret - 1; i > 0; --i) {
    if (client->buffer[i] < 16)
      *(client->buffer + i) = 0;
    else
      break;
  }

  const command_t *command = find_command((const char *)client->buffer);
  if (!command) {
    output(socket, "unable to find command %s\n", client->buffer);
  } else {
    int rc = command->handler(socket);
    if (rc == 1) {
      output(socket, GOODBYE);
      socket_free(socket);
      return;
    }
  }
  output(socket, PROMPT);
}

static void output(socket_t *socket, const char* format, ...) {
  char dest[4096];
  va_list argptr;
  va_start(argptr, format);
  vsprintf(dest, format, argptr);
  va_end(argptr);
  socket_write(socket, dest, strlen(dest));
}

static int help(UNUSED_ATTR socket_t *socket) {
  output(socket, "help command unimplemented\n");
  return 0;
}

static int quit(UNUSED_ATTR socket_t *socket) {
  return 1;
}

static int set(UNUSED_ATTR socket_t *socket) {
  output(socket, "set command unimplemented\n");
  return 0;
}

static int show(socket_t *socket) {
  output(socket, "counter count registered:%d\n", counter_cnt_);
  counter_foreach(monitor_counter_iter_cb, (void *)socket);
  return 0;
}

static const command_t *find_command(const char *name) {
  for  (size_t i = 0; i < ARRAY_SIZE(commands); ++i)
    if (!strcmp(commands[i].name, name))
      return &commands[i];
  return NULL;
}
