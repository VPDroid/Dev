/******************************************************************************
 *
 *  Copyright (C) 2009-2012 Broadcom Corporation
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

#define LOG_TAG "bt_bte_conf"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "bta_api.h"
#include "osi/include/compat.h"
#include "osi/include/config.h"
#include "osi/include/log.h"

#if (defined(BLE_INCLUDED) && (BLE_INCLUDED == TRUE))
extern int btm_ble_tx_power[BTM_BLE_ADV_TX_POWER_MAX + 1];
void bte_load_ble_conf(const char* path)
{
  assert(path != NULL);

  LOG_INFO("%s attempt to load ble stack conf from %s", __func__, path);

  config_t *config = config_new(path);
  if (!config) {
    LOG_INFO("%s file >%s< not found", __func__, path);
    return;
  }

  const char* ble_adv_tx_power = config_get_string(config, CONFIG_DEFAULT_SECTION, "BLE_ADV_TX_POWER", "");
  if(*ble_adv_tx_power) {
    sscanf(ble_adv_tx_power, "%d,%d,%d,%d,%d", btm_ble_tx_power, btm_ble_tx_power + 1, btm_ble_tx_power + 2,
                                               btm_ble_tx_power + 3, btm_ble_tx_power + 4);
    LOG_INFO("loaded btm_ble_tx_power: %d, %d, %d, %d, %d", (char)btm_ble_tx_power[0], (char)btm_ble_tx_power[1],
                                        btm_ble_tx_power[2], btm_ble_tx_power[3], btm_ble_tx_power[4]);
  }
  config_free(config);
}
#endif

// Parses the specified Device ID configuration file and registers the
// Device ID records with SDP.
void bte_load_did_conf(const char *p_path) {
    assert(p_path != NULL);

    config_t *config = config_new(p_path);
    if (!config) {
        LOG_ERROR("%s unable to load DID config '%s'.", __func__, p_path);
        return;
    }

    for (int i = 1; i <= BTA_DI_NUM_MAX; ++i) {
        char section_name[16] = { 0 };
        snprintf(section_name, sizeof(section_name), "DID%d", i);

        if (!config_has_section(config, section_name)) {
            LOG_DEBUG("%s no section named %s.", __func__, section_name);
            break;
        }

        tBTA_DI_RECORD record;
        record.vendor = config_get_int(config, section_name, "vendorId", LMP_COMPID_BROADCOM);
        record.vendor_id_source = config_get_int(config, section_name, "vendorIdSource", DI_VENDOR_ID_SOURCE_BTSIG);
        record.product = config_get_int(config, section_name, "productId", 0);
        record.version = config_get_int(config, section_name, "version", 0);
        record.primary_record = config_get_bool(config, section_name, "primaryRecord", false);
        strlcpy(record.client_executable_url, config_get_string(config, section_name, "clientExecutableURL", ""), sizeof(record.client_executable_url));
        strlcpy(record.service_description, config_get_string(config, section_name, "serviceDescription", ""), sizeof(record.service_description));
        strlcpy(record.documentation_url, config_get_string(config, section_name, "documentationURL", ""), sizeof(record.documentation_url));

        if (record.vendor_id_source != DI_VENDOR_ID_SOURCE_BTSIG &&
            record.vendor_id_source != DI_VENDOR_ID_SOURCE_USBIF) {
            LOG_ERROR("%s invalid vendor id source %d; ignoring DID record %d.", __func__, record.vendor_id_source, i);
            continue;
        }

        LOG_DEBUG("Device ID record %d : %s", i, (record.primary_record ? "primary" : "not primary"));
        LOG_DEBUG("  vendorId            = %04x", record.vendor);
        LOG_DEBUG("  vendorIdSource      = %04x", record.vendor_id_source);
        LOG_DEBUG("  product             = %04x", record.product);
        LOG_DEBUG("  version             = %04x", record.version);
        LOG_DEBUG("  clientExecutableURL = %s", record.client_executable_url);
        LOG_DEBUG("  serviceDescription  = %s", record.service_description);
        LOG_DEBUG("  documentationURL    = %s", record.documentation_url);

        uint32_t record_handle;
        tBTA_STATUS status = BTA_DmSetLocalDiRecord(&record, &record_handle);
        if (status != BTA_SUCCESS) {
            LOG_ERROR("%s unable to set device ID record %d: error %d.", __func__, i, status);
        }
    }

    config_free(config);
}

