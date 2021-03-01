/******************************************************************************
 *
 *  Copyright (C) 2000-2012 Broadcom Corporation
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

#define LOG_TAG "bt_task"

#include <assert.h>

#include "bt_target.h"
#include <pthread.h>
#include <string.h>
#include "dyn_mem.h"

#include "osi/include/alarm.h"
#include "device/include/controller.h"
#include "osi/include/fixed_queue.h"
#include "osi/include/hash_map.h"
#include "btu.h"
#include "btm_int.h"
#include "osi/include/hash_functions.h"
#include "sdpint.h"
#include "osi/include/thread.h"
#include "l2c_int.h"
#include "osi/include/log.h"

#if (BLE_INCLUDED == TRUE)
#include "gatt_api.h"
#include "gatt_int.h"
#if SMP_INCLUDED == TRUE
#include "smp_int.h"
#endif
#endif

// Increase BTU task thread priority to avoid pre-emption
// of audio realated tasks.
#define BTU_TASK_THREAD_PRIORITY -19

extern fixed_queue_t *btif_msg_queue;

// Communication queue from bta thread to bt_workqueue.
fixed_queue_t *btu_bta_msg_queue;

// Communication queue from hci thread to bt_workqueue.
extern fixed_queue_t *btu_hci_msg_queue;

// General timer queue.
fixed_queue_t *btu_general_alarm_queue;
hash_map_t *btu_general_alarm_hash_map;
pthread_mutex_t btu_general_alarm_lock;
static const size_t BTU_GENERAL_ALARM_HASH_MAP_SIZE = 17;

// Oneshot timer queue.
fixed_queue_t *btu_oneshot_alarm_queue;
hash_map_t *btu_oneshot_alarm_hash_map;
pthread_mutex_t btu_oneshot_alarm_lock;
static const size_t BTU_ONESHOT_ALARM_HASH_MAP_SIZE = 17;

// l2cap timer queue.
fixed_queue_t *btu_l2cap_alarm_queue;
hash_map_t *btu_l2cap_alarm_hash_map;
pthread_mutex_t btu_l2cap_alarm_lock;
static const size_t BTU_L2CAP_ALARM_HASH_MAP_SIZE = 17;

thread_t *bt_workqueue_thread;
static const char *BT_WORKQUEUE_NAME = "bt_workqueue";

extern void PLATFORM_DisableHciTransport(UINT8 bDisable);
/*****************************************************************************
**                          V A R I A B L E S                                *
******************************************************************************/
// TODO(cmanton) Move this out of this file
const BD_ADDR   BT_BD_ANY = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

void btu_task_start_up(void *context);
void btu_task_shut_down(void *context);

/*****************************************************************************
**
** Function         btu_init_core
**
** Description      Initialize control block memory for each core component.
**
**
** Returns          void
**
******************************************************************************/
void btu_init_core(void)
{
    /* Initialize the mandatory core stack components */
    btm_init();

    l2c_init();

    sdp_init();

#if BLE_INCLUDED == TRUE
    gatt_init();
#if (defined(SMP_INCLUDED) && SMP_INCLUDED == TRUE)
    SMP_Init();
#endif
    btm_ble_init();
#endif
}

/*****************************************************************************
**
** Function         btu_free_core
**
** Description      Releases control block memory for each core component.
**
**
** Returns          void
**
******************************************************************************/
void btu_free_core(void)
{
      /* Free the mandatory core stack components */
      l2c_free();

#if BLE_INCLUDED == TRUE
      gatt_free();
#endif
}

/*****************************************************************************
**
** Function         BTU_StartUp
**
** Description      Initializes the BTU control block.
**
**                  NOTE: Must be called before creating any tasks
**                      (RPC, BTU, HCIT, APPL, etc.)
**
** Returns          void
**
******************************************************************************/
void BTU_StartUp(void)
{
    memset (&btu_cb, 0, sizeof (tBTU_CB));
    btu_cb.trace_level = HCI_INITIAL_TRACE_LEVEL;

    btu_bta_msg_queue = fixed_queue_new(SIZE_MAX);
    if (btu_bta_msg_queue == NULL)
        goto error_exit;

    btu_general_alarm_hash_map = hash_map_new(BTU_GENERAL_ALARM_HASH_MAP_SIZE,
            hash_function_pointer, NULL, (data_free_fn)alarm_free, NULL);
    if (btu_general_alarm_hash_map == NULL)
        goto error_exit;

    if (pthread_mutex_init(&btu_general_alarm_lock, NULL))
        goto error_exit;

    btu_general_alarm_queue = fixed_queue_new(SIZE_MAX);
    if (btu_general_alarm_queue == NULL)
        goto error_exit;

    btu_oneshot_alarm_hash_map = hash_map_new(BTU_ONESHOT_ALARM_HASH_MAP_SIZE,
            hash_function_pointer, NULL, (data_free_fn)alarm_free, NULL);
    if (btu_oneshot_alarm_hash_map == NULL)
        goto error_exit;

    if (pthread_mutex_init(&btu_oneshot_alarm_lock, NULL))
        goto error_exit;

    btu_oneshot_alarm_queue = fixed_queue_new(SIZE_MAX);
    if (btu_oneshot_alarm_queue == NULL)
        goto error_exit;

    btu_l2cap_alarm_hash_map = hash_map_new(BTU_L2CAP_ALARM_HASH_MAP_SIZE,
            hash_function_pointer, NULL, (data_free_fn)alarm_free, NULL);
    if (btu_l2cap_alarm_hash_map == NULL)
        goto error_exit;

    if (pthread_mutex_init(&btu_l2cap_alarm_lock, NULL))
        goto error_exit;

    btu_l2cap_alarm_queue = fixed_queue_new(SIZE_MAX);
    if (btu_l2cap_alarm_queue == NULL)
         goto error_exit;

    bt_workqueue_thread = thread_new(BT_WORKQUEUE_NAME);
    if (bt_workqueue_thread == NULL)
        goto error_exit;

    thread_set_priority(bt_workqueue_thread, BTU_TASK_THREAD_PRIORITY);

    // Continue startup on bt workqueue thread.
    thread_post(bt_workqueue_thread, btu_task_start_up, NULL);
    return;

  error_exit:;
    LOG_ERROR("%s Unable to allocate resources for bt_workqueue", __func__);
    BTU_ShutDown();
}

void BTU_ShutDown(void) {
  btu_task_shut_down(NULL);

  fixed_queue_free(btu_bta_msg_queue, NULL);

  hash_map_free(btu_general_alarm_hash_map);
  pthread_mutex_destroy(&btu_general_alarm_lock);
  fixed_queue_free(btu_general_alarm_queue, NULL);

  hash_map_free(btu_oneshot_alarm_hash_map);
  pthread_mutex_destroy(&btu_oneshot_alarm_lock);
  fixed_queue_free(btu_oneshot_alarm_queue, NULL);

  hash_map_free(btu_l2cap_alarm_hash_map);
  pthread_mutex_destroy(&btu_l2cap_alarm_lock);
  fixed_queue_free(btu_l2cap_alarm_queue, NULL);

  thread_free(bt_workqueue_thread);

  btu_bta_msg_queue = NULL;

  btu_general_alarm_hash_map = NULL;
  btu_general_alarm_queue = NULL;

  btu_oneshot_alarm_hash_map = NULL;
  btu_oneshot_alarm_queue = NULL;

  btu_l2cap_alarm_hash_map = NULL;
  btu_l2cap_alarm_queue = NULL;

  bt_workqueue_thread = NULL;
}

/*****************************************************************************
**
** Function         BTU_BleAclPktSize
**
** Description      export the BLE ACL packet size.
**
** Returns          UINT16
**
******************************************************************************/
UINT16 BTU_BleAclPktSize(void)
{
#if BLE_INCLUDED == TRUE
    return controller_get_interface()->get_acl_packet_size_ble();
#else
    return 0;
#endif
}

/*******************************************************************************
**
** Function         btu_uipc_rx_cback
**
** Description
**
**
** Returns          void
**
*******************************************************************************/
void btu_uipc_rx_cback(BT_HDR *p_msg) {
  assert(p_msg != NULL);
  BT_TRACE(TRACE_LAYER_BTM, TRACE_TYPE_DEBUG, "btu_uipc_rx_cback event 0x%x,"
      " len %d, offset %d", p_msg->event, p_msg->len, p_msg->offset);
  fixed_queue_enqueue(btu_hci_msg_queue, p_msg);
}
