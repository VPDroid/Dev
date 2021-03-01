/******************************************************************************
 *
 *  Copyright (C) 2001-2012 Broadcom Corporation
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>

#include "bte.h"
#include "bta_api.h"
#include "btu.h"
#include "osi/include/config.h"
#include "gki.h"
#include "l2c_api.h"
#include "osi/include/log.h"
#include "stack_config.h"

#include "port_api.h"
#if (AVDT_INCLUDED==TRUE)
#include "avdt_api.h"
#endif
#include "avrc_api.h"
#if (AVDT_INCLUDED==TRUE)
#include "avdt_api.h"
#endif
#if (A2D_INCLUDED==TRUE)
#include "a2d_api.h"
#endif
#if (BNEP_INCLUDED==TRUE)
#include "bnep_api.h"
#endif
#include "btm_api.h"
#include "gap_api.h"
#if (PAN_INCLUDED==TRUE)
#include "pan_api.h"
#endif
#include "sdp_api.h"

#if (BLE_INCLUDED==TRUE)
#include "gatt_api.h"
#include "smp_api.h"
#endif

#define LOGI0(t,s) __android_log_write(ANDROID_LOG_INFO, t, s)
#define LOGD0(t,s) __android_log_write(ANDROID_LOG_DEBUG, t, s)
#define LOGW0(t,s) __android_log_write(ANDROID_LOG_WARN, t, s)
#define LOGE0(t,s) __android_log_write(ANDROID_LOG_ERROR, t, s)

#ifndef DEFAULT_CONF_TRACE_LEVEL
#define DEFAULT_CONF_TRACE_LEVEL BT_TRACE_LEVEL_WARNING
#endif

#ifndef BTE_LOG_BUF_SIZE
#define BTE_LOG_BUF_SIZE  1024
#endif

#define BTE_LOG_MAX_SIZE  (BTE_LOG_BUF_SIZE - 12)

#define MSG_BUFFER_OFFSET 0

/* LayerIDs for BTA, currently everything maps onto appl_trace_level */
static const char * const bt_layer_tags[] = {
  "bt_btif",
  "bt_usb",
  "bt_serial",
  "bt_socket",
  "bt_rs232",
  "bt_lc",
  "bt_lm",
  "bt_hci",
  "bt_l2cap",
  "bt_rfcomm",
  "bt_sdp",
  "bt_tcs",
  "bt_obex",
  "bt_btm",
  "bt_gap",
  "UNUSED",
  "UNUSED",
  "bt_icp",
  "bt_hsp2",
  "bt_spp",
  "bt_ctp",
  "bt_bpp",
  "bt_hcrp",
  "bt_ftp",
  "bt_opp",
  "bt_btu",
  "bt_gki",
  "bt_bnep",
  "bt_pan",
  "bt_hfp",
  "bt_hid",
  "bt_bip",
  "bt_avp",
  "bt_a2d",
  "bt_sap",
  "bt_amp",
  "bt_mca",
  "bt_att",
  "bt_smp",
  "bt_nfc",
  "bt_nci",
  "bt_idep",
  "bt_ndep",
  "bt_llcp",
  "bt_rw",
  "bt_ce",
  "bt_snep",
  "bt_ndef",
  "bt_nfa",
};
static uint8_t BTAPP_SetTraceLevel(uint8_t new_level);
static uint8_t BTIF_SetTraceLevel(uint8_t new_level);
static uint8_t BTU_SetTraceLevel(uint8_t new_level);

/* make sure list is order by increasing layer id!!! */
static tBTTRC_FUNC_MAP bttrc_set_level_map[] = {
  {BTTRC_ID_STK_BTU, BTTRC_ID_STK_HCI, BTU_SetTraceLevel, "TRC_HCI", DEFAULT_CONF_TRACE_LEVEL},
  {BTTRC_ID_STK_L2CAP, BTTRC_ID_STK_L2CAP, L2CA_SetTraceLevel, "TRC_L2CAP", DEFAULT_CONF_TRACE_LEVEL},
  {BTTRC_ID_STK_RFCOMM, BTTRC_ID_STK_RFCOMM_DATA, PORT_SetTraceLevel, "TRC_RFCOMM", DEFAULT_CONF_TRACE_LEVEL},
#if (AVDT_INCLUDED==TRUE)
  {BTTRC_ID_STK_AVDT, BTTRC_ID_STK_AVDT, AVDT_SetTraceLevel, "TRC_AVDT", DEFAULT_CONF_TRACE_LEVEL},
#endif
  {BTTRC_ID_STK_AVRC, BTTRC_ID_STK_AVRC, AVRC_SetTraceLevel, "TRC_AVRC", DEFAULT_CONF_TRACE_LEVEL},
#if (AVDT_INCLUDED==TRUE)
  //{BTTRC_ID_AVDT_SCB, BTTRC_ID_AVDT_CCB, NULL, "TRC_AVDT_SCB", DEFAULT_CONF_TRACE_LEVEL},
#endif
#if (A2D_INCLUDED==TRUE)
  {BTTRC_ID_STK_A2D, BTTRC_ID_STK_A2D, A2D_SetTraceLevel, "TRC_A2D", DEFAULT_CONF_TRACE_LEVEL},
#endif
#if (BNEP_INCLUDED==TRUE)
  {BTTRC_ID_STK_BNEP, BTTRC_ID_STK_BNEP, BNEP_SetTraceLevel, "TRC_BNEP", DEFAULT_CONF_TRACE_LEVEL},
#endif
  {BTTRC_ID_STK_BTM_ACL, BTTRC_ID_STK_BTM_SEC, BTM_SetTraceLevel, "TRC_BTM", DEFAULT_CONF_TRACE_LEVEL},
  {BTTRC_ID_STK_GAP, BTTRC_ID_STK_GAP, GAP_SetTraceLevel, "TRC_GAP", DEFAULT_CONF_TRACE_LEVEL},
#if (PAN_INCLUDED==TRUE)
  {BTTRC_ID_STK_PAN, BTTRC_ID_STK_PAN, PAN_SetTraceLevel, "TRC_PAN", DEFAULT_CONF_TRACE_LEVEL},
#endif
  {BTTRC_ID_STK_SDP, BTTRC_ID_STK_SDP, SDP_SetTraceLevel, "TRC_SDP", DEFAULT_CONF_TRACE_LEVEL},
#if (BLE_INCLUDED==TRUE)
  {BTTRC_ID_STK_GATT, BTTRC_ID_STK_GATT, GATT_SetTraceLevel, "TRC_GATT", DEFAULT_CONF_TRACE_LEVEL},
  {BTTRC_ID_STK_SMP, BTTRC_ID_STK_SMP, SMP_SetTraceLevel, "TRC_SMP", DEFAULT_CONF_TRACE_LEVEL},
#endif

  /* LayerIDs for BTA, currently everything maps onto appl_trace_level.
   */
  {BTTRC_ID_BTA_ACC, BTTRC_ID_BTAPP, BTAPP_SetTraceLevel, "TRC_BTAPP", DEFAULT_CONF_TRACE_LEVEL},
  {BTTRC_ID_BTA_ACC, BTTRC_ID_BTAPP, BTIF_SetTraceLevel, "TRC_BTIF", DEFAULT_CONF_TRACE_LEVEL},

  {0, 0, NULL, NULL, DEFAULT_CONF_TRACE_LEVEL}
};

static const UINT16 bttrc_map_size = sizeof(bttrc_set_level_map)/sizeof(tBTTRC_FUNC_MAP);

void LogMsg(uint32_t trace_set_mask, const char *fmt_str, ...) {
  static char buffer[BTE_LOG_BUF_SIZE];
  int trace_layer = TRACE_GET_LAYER(trace_set_mask);
  if (trace_layer >= TRACE_LAYER_MAX_NUM)
    trace_layer = 0;

  va_list ap;
  va_start(ap, fmt_str);
  vsnprintf(&buffer[MSG_BUFFER_OFFSET], BTE_LOG_MAX_SIZE, fmt_str, ap);
  va_end(ap);

  switch ( TRACE_GET_TYPE(trace_set_mask) ) {
    case TRACE_TYPE_ERROR:
      LOGE0(bt_layer_tags[trace_layer], buffer);
      break;
    case TRACE_TYPE_WARNING:
      LOGW0(bt_layer_tags[trace_layer], buffer);
      break;
    case TRACE_TYPE_API:
    case TRACE_TYPE_EVENT:
      LOGI0(bt_layer_tags[trace_layer], buffer);
      break;
    case TRACE_TYPE_DEBUG:
      LOGD0(bt_layer_tags[trace_layer], buffer);
      break;
    default:
      LOGE0(bt_layer_tags[trace_layer], buffer);      /* we should never get this */
      break;
  }
}

/* this function should go into BTAPP_DM for example */
static uint8_t BTAPP_SetTraceLevel(uint8_t new_level) {
  if (new_level != 0xFF)
    appl_trace_level = new_level;

  return appl_trace_level;
}

static uint8_t BTIF_SetTraceLevel(uint8_t new_level) {
  if (new_level != 0xFF)
    btif_trace_level = new_level;

  return btif_trace_level;
}

static uint8_t BTU_SetTraceLevel(uint8_t new_level) {
  if (new_level != 0xFF)
    btu_cb.trace_level = new_level;

  return btu_cb.trace_level;
}

static void load_levels_from_config(const config_t *config) {
  assert(config != NULL);

  for (tBTTRC_FUNC_MAP *functions = &bttrc_set_level_map[0]; functions->trc_name; ++functions) {
    LOG_INFO("BTE_InitTraceLevels -- %s", functions->trc_name);
    int value = config_get_int(config, CONFIG_DEFAULT_SECTION, functions->trc_name, -1);
    if (value != -1)
      functions->trace_level = value;

    if (functions->p_f)
      functions->p_f(functions->trace_level);
  }
}

static future_t *init(void) {
  const stack_config_t *stack_config = stack_config_get_interface();
  if (!stack_config->get_trace_config_enabled()) {
    LOG_INFO("[bttrc] using compile default trace settings");
    return NULL;
  }

  load_levels_from_config(stack_config->get_all());
  return NULL;
}

const module_t bte_logmsg_module = {
  .name = BTE_LOGMSG_MODULE,
  .init = init,
  .start_up = NULL,
  .shut_down = NULL,
  .clean_up = NULL,
  .dependencies = {
    STACK_CONFIG_MODULE,
    NULL
  }
};
