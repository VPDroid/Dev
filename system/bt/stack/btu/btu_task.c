/******************************************************************************
 *
 *  Copyright (C) 1999-2012 Broadcom Corporation
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

#define LOG_TAG "bt_btu_task"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "osi/include/alarm.h"
#include "bt_target.h"
#include "bt_trace.h"
#include "bt_types.h"
#include "bt_utils.h"
#include "btif_common.h"
#include "btm_api.h"
#include "btm_int.h"
#include "btu.h"
#include "osi/include/fixed_queue.h"
#include "osi/include/future.h"
#include "gki.h"
#include "osi/include/hash_map.h"
#include "hcimsgs.h"
#include "l2c_int.h"
#include "btcore/include/module.h"
#include "osi/include/osi.h"
#include "osi/include/log.h"
#include "sdpint.h"
#include "osi/include/thread.h"

#include "port_api.h"
#include "port_ext.h"

#include "gap_int.h"

#if (defined(BNEP_INCLUDED) && BNEP_INCLUDED == TRUE)
#include "bnep_int.h"
#endif

#if (defined(PAN_INCLUDED) && PAN_INCLUDED == TRUE)
#include "pan_int.h"
#endif

#if (defined(HID_HOST_INCLUDED) && HID_HOST_INCLUDED == TRUE )
#include "hidh_int.h"
#endif

#if (defined(AVDT_INCLUDED) && AVDT_INCLUDED == TRUE)
#include "avdt_int.h"
#else
extern void avdt_rcv_sync_info (BT_HDR *p_buf); /* this is for hci_test */
#endif

#if (defined(MCA_INCLUDED) && MCA_INCLUDED == TRUE)
#include "mca_api.h"
#include "mca_defs.h"
#include "mca_int.h"
#endif

#include "bta_sys.h"

#if (BLE_INCLUDED == TRUE)
#include "gatt_int.h"
#if (SMP_INCLUDED == TRUE)
#include "smp_int.h"
#endif
#include "btm_ble_int.h"
#endif

extern void BTE_InitStack(void);

/* Define BTU storage area
*/
#if BTU_DYNAMIC_MEMORY == FALSE
tBTU_CB  btu_cb;
#endif

// Communication queue between btu_task and bta.
extern fixed_queue_t *btu_bta_msg_queue;

// Communication queue between btu_task and hci.
extern fixed_queue_t *btu_hci_msg_queue;

// General timer queue.
extern fixed_queue_t *btu_general_alarm_queue;
extern hash_map_t *btu_general_alarm_hash_map;
extern pthread_mutex_t btu_general_alarm_lock;

// Oneshot timer queue.
extern fixed_queue_t *btu_oneshot_alarm_queue;
extern hash_map_t *btu_oneshot_alarm_hash_map;
extern pthread_mutex_t btu_oneshot_alarm_lock;

// l2cap timer queue.
extern fixed_queue_t *btu_l2cap_alarm_queue;
extern hash_map_t *btu_l2cap_alarm_hash_map;
extern pthread_mutex_t btu_l2cap_alarm_lock;

extern fixed_queue_t *event_queue;
extern fixed_queue_t *btif_msg_queue;

extern thread_t *bt_workqueue_thread;

/* Define a function prototype to allow a generic timeout handler */
typedef void (tUSER_TIMEOUT_FUNC) (TIMER_LIST_ENT *p_tle);

static void btu_l2cap_alarm_process(TIMER_LIST_ENT *p_tle);
static void btu_general_alarm_process(TIMER_LIST_ENT *p_tle);
static void btu_bta_alarm_process(TIMER_LIST_ENT *p_tle);
static void btu_hci_msg_process(BT_HDR *p_msg);

void btu_hci_msg_ready(fixed_queue_t *queue, UNUSED_ATTR void *context) {
    BT_HDR *p_msg = (BT_HDR *)fixed_queue_dequeue(queue);
    btu_hci_msg_process(p_msg);
}

void btu_general_alarm_ready(fixed_queue_t *queue, UNUSED_ATTR void *context) {
    TIMER_LIST_ENT *p_tle = (TIMER_LIST_ENT *)fixed_queue_dequeue(queue);
    btu_general_alarm_process(p_tle);
}

void btu_oneshot_alarm_ready(fixed_queue_t *queue, UNUSED_ATTR void *context) {
    TIMER_LIST_ENT *p_tle = (TIMER_LIST_ENT *)fixed_queue_dequeue(queue);
    btu_general_alarm_process(p_tle);

    switch (p_tle->event) {
#if (defined(BLE_INCLUDED) && BLE_INCLUDED == TRUE)
        case BTU_TTYPE_BLE_RANDOM_ADDR:
            btm_ble_timeout(p_tle);
            break;
#endif

        case BTU_TTYPE_USER_FUNC:
            {
                tUSER_TIMEOUT_FUNC  *p_uf = (tUSER_TIMEOUT_FUNC *)p_tle->param;
                (*p_uf)(p_tle);
            }
            break;

        default:
            // FAIL
            BTM_TRACE_WARNING("Received unexpected oneshot timer event:0x%x\n",
                p_tle->event);
            break;
    }
}

void btu_l2cap_alarm_ready(fixed_queue_t *queue, UNUSED_ATTR void *context) {
    TIMER_LIST_ENT *p_tle = (TIMER_LIST_ENT *)fixed_queue_dequeue(queue);
    btu_l2cap_alarm_process(p_tle);
}

void btu_bta_msg_ready(fixed_queue_t *queue, UNUSED_ATTR void *context) {
    BT_HDR *p_msg = (BT_HDR *)fixed_queue_dequeue(queue);
    bta_sys_event(p_msg);
}

void btu_bta_alarm_ready(fixed_queue_t *queue, UNUSED_ATTR void *context) {
    TIMER_LIST_ENT *p_tle = (TIMER_LIST_ENT *)fixed_queue_dequeue(queue);
    btu_bta_alarm_process(p_tle);
}

static void btu_hci_msg_process(BT_HDR *p_msg) {
    /* Determine the input message type. */
    switch (p_msg->event & BT_EVT_MASK)
    {
        case BTU_POST_TO_TASK_NO_GOOD_HORRIBLE_HACK: // TODO(zachoverflow): remove this
            ((post_to_task_hack_t *)(&p_msg->data[0]))->callback(p_msg);
            break;
        case BT_EVT_TO_BTU_HCI_ACL:
            /* All Acl Data goes to L2CAP */
            l2c_rcv_acl_data (p_msg);
            break;

        case BT_EVT_TO_BTU_L2C_SEG_XMIT:
            /* L2CAP segment transmit complete */
            l2c_link_segments_xmitted (p_msg);
            break;

        case BT_EVT_TO_BTU_HCI_SCO:
#if BTM_SCO_INCLUDED == TRUE
            btm_route_sco_data (p_msg);
            break;
#endif

        case BT_EVT_TO_BTU_HCI_EVT:
            btu_hcif_process_event ((UINT8)(p_msg->event & BT_SUB_EVT_MASK), p_msg);
            GKI_freebuf(p_msg);

#if (defined(HCILP_INCLUDED) && HCILP_INCLUDED == TRUE)
            /* If host receives events which it doesn't response to, */
            /* host should start idle timer to enter sleep mode.     */
            btu_check_bt_sleep ();
#endif
            break;

        case BT_EVT_TO_BTU_HCI_CMD:
            btu_hcif_send_cmd ((UINT8)(p_msg->event & BT_SUB_EVT_MASK), p_msg);
            break;

        default:;
            int i = 0;
            uint16_t mask = (UINT16) (p_msg->event & BT_EVT_MASK);
            BOOLEAN handled = FALSE;

            for (; !handled && i < BTU_MAX_REG_EVENT; i++)
            {
                if (btu_cb.event_reg[i].event_cb == NULL)
                    continue;

                if (mask == btu_cb.event_reg[i].event_range)
                {
                    if (btu_cb.event_reg[i].event_cb)
                    {
                        btu_cb.event_reg[i].event_cb(p_msg);
                        handled = TRUE;
                    }
                }
            }

            if (handled == FALSE)
                GKI_freebuf (p_msg);

            break;
    }

}

static void btu_bta_alarm_process(TIMER_LIST_ENT *p_tle) {
    /* call timer callback */
    if (p_tle->p_cback) {
        (*p_tle->p_cback)(p_tle);
    } else if (p_tle->event) {
        BT_HDR *p_msg;
        if ((p_msg = (BT_HDR *) GKI_getbuf(sizeof(BT_HDR))) != NULL) {
            p_msg->event = p_tle->event;
            p_msg->layer_specific = 0;
            bta_sys_sendmsg(p_msg);
        }
    }
}

void btu_task_start_up(UNUSED_ATTR void *context) {
  BT_TRACE(TRACE_LAYER_BTU, TRACE_TYPE_API,
      "btu_task pending for preload complete event");

  LOG_INFO("Bluetooth chip preload is complete");

  BT_TRACE(TRACE_LAYER_BTU, TRACE_TYPE_API,
      "btu_task received preload complete event");

  /* Initialize the mandatory core stack control blocks
     (BTU, BTM, L2CAP, and SDP)
   */
  btu_init_core();

  /* Initialize any optional stack components */
  BTE_InitStack();

  bta_sys_init();

  /* Initialise platform trace levels at this point as BTE_InitStack() and bta_sys_init()
   * reset the control blocks and preset the trace level with XXX_INITIAL_TRACE_LEVEL
   */
#if ( BT_USE_TRACES==TRUE )
  module_init(get_module(BTE_LOGMSG_MODULE));
#endif

  // Inform the bt jni thread initialization is ok.
  btif_transfer_context(btif_init_ok, 0, NULL, 0, NULL);

  fixed_queue_register_dequeue(btu_bta_msg_queue,
      thread_get_reactor(bt_workqueue_thread),
      btu_bta_msg_ready,
      NULL);

  fixed_queue_register_dequeue(btu_hci_msg_queue,
      thread_get_reactor(bt_workqueue_thread),
      btu_hci_msg_ready,
      NULL);

  fixed_queue_register_dequeue(btu_general_alarm_queue,
      thread_get_reactor(bt_workqueue_thread),
      btu_general_alarm_ready,
      NULL);

  fixed_queue_register_dequeue(btu_oneshot_alarm_queue,
      thread_get_reactor(bt_workqueue_thread),
      btu_oneshot_alarm_ready,
      NULL);

  fixed_queue_register_dequeue(btu_l2cap_alarm_queue,
      thread_get_reactor(bt_workqueue_thread),
      btu_l2cap_alarm_ready,
      NULL);
}

void btu_task_shut_down(UNUSED_ATTR void *context) {
  fixed_queue_unregister_dequeue(btu_bta_msg_queue);
  fixed_queue_unregister_dequeue(btu_hci_msg_queue);
  fixed_queue_unregister_dequeue(btu_general_alarm_queue);
  fixed_queue_unregister_dequeue(btu_oneshot_alarm_queue);
  fixed_queue_unregister_dequeue(btu_l2cap_alarm_queue);

#if ( BT_USE_TRACES==TRUE )
  module_clean_up(get_module(BTE_LOGMSG_MODULE));
#endif

  bta_sys_free();
  btu_free_core();
}

/*******************************************************************************
**
** Function         btu_start_timer
**
** Description      Start a timer for the specified amount of time.
**                  NOTE: The timeout resolution is in SECONDS! (Even
**                          though the timer structure field is ticks)
**
** Returns          void
**
*******************************************************************************/
static void btu_general_alarm_process(TIMER_LIST_ENT *p_tle) {
    assert(p_tle != NULL);

    switch (p_tle->event) {
        case BTU_TTYPE_BTM_DEV_CTL:
            btm_dev_timeout(p_tle);
            break;

        case BTU_TTYPE_L2CAP_LINK:
        case BTU_TTYPE_L2CAP_CHNL:
        case BTU_TTYPE_L2CAP_HOLD:
        case BTU_TTYPE_L2CAP_INFO:
        case BTU_TTYPE_L2CAP_FCR_ACK:
            l2c_process_timeout (p_tle);
            break;

        case BTU_TTYPE_SDP:
            sdp_conn_timeout ((tCONN_CB *)p_tle->param);
            break;

        case BTU_TTYPE_BTM_RMT_NAME:
            btm_inq_rmt_name_failed();
            break;

        case BTU_TTYPE_RFCOMM_MFC:
        case BTU_TTYPE_RFCOMM_PORT:
            rfcomm_process_timeout (p_tle);
            break;

#if ((defined(BNEP_INCLUDED) && BNEP_INCLUDED == TRUE))
        case BTU_TTYPE_BNEP:
            bnep_process_timeout(p_tle);
            break;
#endif


#if (defined(AVDT_INCLUDED) && AVDT_INCLUDED == TRUE)
        case BTU_TTYPE_AVDT_CCB_RET:
        case BTU_TTYPE_AVDT_CCB_RSP:
        case BTU_TTYPE_AVDT_CCB_IDLE:
        case BTU_TTYPE_AVDT_SCB_TC:
            avdt_process_timeout(p_tle);
            break;
#endif

#if (defined(HID_HOST_INCLUDED) && HID_HOST_INCLUDED == TRUE)
        case BTU_TTYPE_HID_HOST_REPAGE_TO :
            hidh_proc_repage_timeout(p_tle);
            break;
#endif

#if (defined(BLE_INCLUDED) && BLE_INCLUDED == TRUE)
        case BTU_TTYPE_BLE_INQUIRY:
        case BTU_TTYPE_BLE_GAP_LIM_DISC:
        case BTU_TTYPE_BLE_RANDOM_ADDR:
        case BTU_TTYPE_BLE_GAP_FAST_ADV:
        case BTU_TTYPE_BLE_OBSERVE:
            btm_ble_timeout(p_tle);
            break;

        case BTU_TTYPE_ATT_WAIT_FOR_RSP:
            gatt_rsp_timeout(p_tle);
            break;

        case BTU_TTYPE_ATT_WAIT_FOR_IND_ACK:
            gatt_ind_ack_timeout(p_tle);
            break;
#if (defined(SMP_INCLUDED) && SMP_INCLUDED == TRUE)
        case BTU_TTYPE_SMP_PAIRING_CMD:
            smp_rsp_timeout(p_tle);
            break;
#endif

#endif

#if (MCA_INCLUDED == TRUE)
        case BTU_TTYPE_MCA_CCB_RSP:
            mca_process_timeout(p_tle);
            break;
#endif
        case BTU_TTYPE_USER_FUNC:
            {
                tUSER_TIMEOUT_FUNC  *p_uf = (tUSER_TIMEOUT_FUNC *)p_tle->param;
                (*p_uf)(p_tle);
            }
            break;

        default:;
                int i = 0;
                BOOLEAN handled = FALSE;

                for (; !handled && i < BTU_MAX_REG_TIMER; i++)
                {
                    if (btu_cb.timer_reg[i].timer_cb == NULL)
                        continue;
                    if (btu_cb.timer_reg[i].p_tle == p_tle)
                    {
                        btu_cb.timer_reg[i].timer_cb(p_tle);
                        handled = TRUE;
                    }
                }
                break;
    }
}

void btu_general_alarm_cb(void *data) {
  assert(data != NULL);
  TIMER_LIST_ENT *p_tle = (TIMER_LIST_ENT *)data;

  fixed_queue_enqueue(btu_general_alarm_queue, p_tle);
}

void btu_start_timer(TIMER_LIST_ENT *p_tle, UINT16 type, UINT32 timeout_sec) {
  assert(p_tle != NULL);

  // Get the alarm for the timer list entry.
  pthread_mutex_lock(&btu_general_alarm_lock);
  if (!hash_map_has_key(btu_general_alarm_hash_map, p_tle)) {
    hash_map_set(btu_general_alarm_hash_map, p_tle, alarm_new());
  }
  pthread_mutex_unlock(&btu_general_alarm_lock);

  alarm_t *alarm = hash_map_get(btu_general_alarm_hash_map, p_tle);
  if (alarm == NULL) {
    LOG_ERROR("%s Unable to create alarm", __func__);
    return;
  }
  alarm_cancel(alarm);

  p_tle->event = type;
  // NOTE: This value is in seconds but stored in a ticks field.
  p_tle->ticks = timeout_sec;
  p_tle->in_use = TRUE;
  alarm_set(alarm, (period_ms_t)(timeout_sec * 1000), btu_general_alarm_cb, (void *)p_tle);
}

/*******************************************************************************
**
** Function         btu_stop_timer
**
** Description      Stop a timer.
**
** Returns          void
**
*******************************************************************************/
void btu_stop_timer(TIMER_LIST_ENT *p_tle) {
  assert(p_tle != NULL);

  if (p_tle->in_use == FALSE)
    return;
  p_tle->in_use = FALSE;

  // Get the alarm for the timer list entry.
  alarm_t *alarm = hash_map_get(btu_general_alarm_hash_map, p_tle);
  if (alarm == NULL) {
    LOG_WARN("%s Unable to find expected alarm in hashmap", __func__);
    return;
  }
  alarm_cancel(alarm);
}

#if defined(QUICK_TIMER_TICKS_PER_SEC) && (QUICK_TIMER_TICKS_PER_SEC > 0)
/*******************************************************************************
**
** Function         btu_start_quick_timer
**
** Description      Start a timer for the specified amount of time in ticks.
**
** Returns          void
**
*******************************************************************************/
static void btu_l2cap_alarm_process(TIMER_LIST_ENT *p_tle) {
  assert(p_tle != NULL);

  switch (p_tle->event) {
    case BTU_TTYPE_L2CAP_CHNL:      /* monitor or retransmission timer */
    case BTU_TTYPE_L2CAP_FCR_ACK:   /* ack timer */
      l2c_process_timeout (p_tle);
      break;

    default:
      break;
  }
}

static void btu_l2cap_alarm_cb(void *data) {
  assert(data != NULL);
  TIMER_LIST_ENT *p_tle = (TIMER_LIST_ENT *)data;

  fixed_queue_enqueue(btu_l2cap_alarm_queue, p_tle);
}

void btu_start_quick_timer(TIMER_LIST_ENT *p_tle, UINT16 type, UINT32 timeout_ticks) {
  assert(p_tle != NULL);

  // Get the alarm for the timer list entry.
  pthread_mutex_lock(&btu_l2cap_alarm_lock);
  if (!hash_map_has_key(btu_l2cap_alarm_hash_map, p_tle)) {
    hash_map_set(btu_l2cap_alarm_hash_map, p_tle, alarm_new());
  }
  pthread_mutex_unlock(&btu_l2cap_alarm_lock);

  alarm_t *alarm = hash_map_get(btu_l2cap_alarm_hash_map, p_tle);
  if (alarm == NULL) {
    LOG_ERROR("%s Unable to create alarm", __func__);
    return;
  }
  alarm_cancel(alarm);

  p_tle->event = type;
  p_tle->ticks = timeout_ticks;
  p_tle->in_use = TRUE;
  // The quick timer ticks are 100ms long.
  alarm_set(alarm, (period_ms_t)(timeout_ticks * 100), btu_l2cap_alarm_cb, (void *)p_tle);
}

/*******************************************************************************
**
** Function         btu_stop_quick_timer
**
** Description      Stop a timer.
**
** Returns          void
**
*******************************************************************************/
void btu_stop_quick_timer(TIMER_LIST_ENT *p_tle) {
  assert(p_tle != NULL);

  if (p_tle->in_use == FALSE)
    return;
  p_tle->in_use = FALSE;

  // Get the alarm for the timer list entry.
  alarm_t *alarm = hash_map_get(btu_l2cap_alarm_hash_map, p_tle);
  if (alarm == NULL) {
    LOG_WARN("%s Unable to find expected alarm in hashmap", __func__);
    return;
  }
  alarm_cancel(alarm);
}
#endif /* defined(QUICK_TIMER_TICKS_PER_SEC) && (QUICK_TIMER_TICKS_PER_SEC > 0) */

void btu_oneshot_alarm_cb(void *data) {
  assert(data != NULL);
  TIMER_LIST_ENT *p_tle = (TIMER_LIST_ENT *)data;

  btu_stop_timer_oneshot(p_tle);

  fixed_queue_enqueue(btu_oneshot_alarm_queue, p_tle);
}

/*
 * Starts a oneshot timer with a timeout in seconds.
 */
void btu_start_timer_oneshot(TIMER_LIST_ENT *p_tle, UINT16 type, UINT32 timeout_sec) {
  assert(p_tle != NULL);

  // Get the alarm for the timer list entry.
  pthread_mutex_lock(&btu_oneshot_alarm_lock);
  if (!hash_map_has_key(btu_oneshot_alarm_hash_map, p_tle)) {
    hash_map_set(btu_oneshot_alarm_hash_map, p_tle, alarm_new());
  }
  pthread_mutex_unlock(&btu_oneshot_alarm_lock);

  alarm_t *alarm = hash_map_get(btu_oneshot_alarm_hash_map, p_tle);
  if (alarm == NULL) {
    LOG_ERROR("%s Unable to create alarm", __func__);
    return;
  }
  alarm_cancel(alarm);

  p_tle->event = type;
  p_tle->in_use = TRUE;
  // NOTE: This value is in seconds but stored in a ticks field.
  p_tle->ticks = timeout_sec;
  alarm_set(alarm, (period_ms_t)(timeout_sec * 1000), btu_oneshot_alarm_cb, (void *)p_tle);
}

void btu_stop_timer_oneshot(TIMER_LIST_ENT *p_tle) {
  assert(p_tle != NULL);

  if (p_tle->in_use == FALSE)
    return;
  p_tle->in_use = FALSE;

  // Get the alarm for the timer list entry.
  alarm_t *alarm = hash_map_get(btu_oneshot_alarm_hash_map, p_tle);
  if (alarm == NULL) {
    LOG_WARN("%s Unable to find expected alarm in hashmap", __func__);
    return;
  }
  alarm_cancel(alarm);
}

#if (defined(HCILP_INCLUDED) && HCILP_INCLUDED == TRUE)
/*******************************************************************************
**
** Function         btu_check_bt_sleep
**
** Description      This function is called to check if controller can go to sleep.
**
** Returns          void
**
*******************************************************************************/
void btu_check_bt_sleep (void)
{
    // TODO(zachoverflow) take pending commands into account?
    if (l2cb.controller_xmit_window == l2cb.num_lm_acl_bufs)
    {
        bte_main_lpm_allow_bt_device_sleep();
    }
}
#endif
