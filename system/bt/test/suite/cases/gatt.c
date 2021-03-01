#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "base.h"
#include "support/gatt.h"
#include "support/callbacks.h"

#define DEFAULT_RANDOM_SEED 42

static void create_random_uuid(bt_uuid_t *uuid, int seed) {
  srand(seed < 0 ? time(NULL) : seed);
  for (int i = 0; i < 16; ++i) {
    uuid->uu[i] = (uint8_t) (rand() % 256);
  }
}

bool gatt_client_register() {
  TASSERT(gatt_interface != NULL, "Null GATT interface.");

  // Registers gatt client.
  bt_uuid_t gatt_client_uuid;
  create_random_uuid(&gatt_client_uuid, DEFAULT_RANDOM_SEED);
  CALL_AND_WAIT(gatt_interface->client->register_client(&gatt_client_uuid), btgattc_register_app_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error registering GATT client app callback.");

  // Unregisters gatt client. No callback is expected.
  gatt_interface->client->unregister_client(gatt_get_client_interface());

  return true;
}

bool gatt_client_scan() {
  TASSERT(gatt_interface != NULL, "Null GATT interface.");

  // Starts BLE scan. NB: This test assumes there is a BLE beacon advertising nearby.
  CALL_AND_WAIT(gatt_interface->client->scan(true), btgattc_scan_result_cb);

  // Ends BLE scan. No callback is expected.
  gatt_interface->client->scan(false);

  return true;
}

bool gatt_client_advertise() {
  TASSERT(gatt_interface != NULL, "Null GATT interface.");

  // Registers a new client app.
  bt_uuid_t gatt_client_uuid;
  create_random_uuid(&gatt_client_uuid, DEFAULT_RANDOM_SEED);
  CALL_AND_WAIT(gatt_interface->client->register_client(&gatt_client_uuid), btgattc_register_app_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error registering GATT client app callback.");

  // Starts advertising.
  CALL_AND_WAIT(gatt_interface->client->listen(gatt_get_client_interface(), true), btgattc_advertise_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error starting BLE advertisement.");

  // Stops advertising.
  CALL_AND_WAIT(gatt_interface->client->listen(gatt_get_client_interface(), false), btgattc_advertise_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error stopping BLE advertisement.");

  // Unregisters gatt server. No callback is expected.
  gatt_interface->client->unregister_client(gatt_get_client_interface());

  return true;
}

bool gatt_server_register() {
  TASSERT(gatt_interface != NULL, "Null GATT interface.");

  // Registers gatt server.
  bt_uuid_t gatt_server_uuid;
  create_random_uuid(&gatt_server_uuid, DEFAULT_RANDOM_SEED);
  CALL_AND_WAIT(gatt_interface->server->register_server(&gatt_server_uuid), btgatts_register_app_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error registering GATT server app callback.");

  // Unregisters gatt server. No callback is expected.
  gatt_interface->server->unregister_server(gatt_get_server_interface());
  return true;
}

bool gatt_server_build() {
  TASSERT(gatt_interface != NULL, "Null GATT interface.");

  // Registers gatt server.
  bt_uuid_t gatt_server_uuid;
  create_random_uuid(&gatt_server_uuid, DEFAULT_RANDOM_SEED);
  CALL_AND_WAIT(gatt_interface->server->register_server(&gatt_server_uuid), btgatts_register_app_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error registering GATT server app callback.");

  // Service UUID.
  btgatt_srvc_id_t srvc_id;
  srvc_id.id.inst_id = 0;   // there is only one instance of this service.
  srvc_id.is_primary = 1;   // this service is primary.
  create_random_uuid(&srvc_id.id.uuid, -1);

  // Characteristics UUID.
  bt_uuid_t char_uuid;
  create_random_uuid(&char_uuid, -1);

  // Descriptor UUID.
  bt_uuid_t desc_uuid;
  create_random_uuid(&desc_uuid, -1);

  // Adds service.
  int server_if = gatt_get_server_interface();
  CALL_AND_WAIT(gatt_interface->server->add_service(server_if, &srvc_id, 4 /* # handles */), btgatts_service_added_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error adding service.");

  // Adds characteristics.
  int srvc_handle = gatt_get_service_handle();
  CALL_AND_WAIT(gatt_interface->server->add_characteristic(server_if, srvc_handle, &char_uuid, 0x10 /* notification */, 0x01 /* read only */), btgatts_characteristic_added_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error adding characteristics.");

  // Adds descriptor.
  CALL_AND_WAIT(gatt_interface->server->add_descriptor(server_if, srvc_handle, &desc_uuid, 0x01), btgatts_descriptor_added_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error adding descriptor.");

  // Starts server.
  CALL_AND_WAIT(gatt_interface->server->start_service(server_if, srvc_handle, 2 /*BREDR/LE*/), btgatts_service_started_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error starting server.");

  // Stops server.
  CALL_AND_WAIT(gatt_interface->server->stop_service(server_if, srvc_handle), btgatts_service_stopped_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error stopping server.");

  // Deletes service.
  CALL_AND_WAIT(gatt_interface->server->delete_service(server_if, srvc_handle), btgatts_service_deleted_cb);
  TASSERT(gatt_get_status() == BT_STATUS_SUCCESS, "Error deleting service.");

  // Unregisters gatt server. No callback is expected.
  gatt_interface->server->unregister_server(server_if);

  return true;
}
