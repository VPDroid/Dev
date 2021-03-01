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

/******************************************************************************
 *
 *  This module contains the routines that initialize the stack components.
 *  It must be called before the BTU task is started.
 *
 ******************************************************************************/

#include "bt_target.h"
#include <string.h>

#ifndef BTA_INCLUDED
#define BTA_INCLUDED FALSE
#endif

/* Include initialization functions definitions */
#include "port_api.h"

#if (defined(BNEP_INCLUDED) && BNEP_INCLUDED == TRUE)
#include "bnep_api.h"
#endif

#include "gap_api.h"

#if (defined(PAN_INCLUDED) && PAN_INCLUDED == TRUE)
#include "pan_api.h"
#endif

#include "avrc_api.h"

#if (defined(A2D_INCLUDED) && A2D_INCLUDED == TRUE)
#include "a2d_api.h"
#endif

#if (defined(HID_HOST_INCLUDED) && HID_HOST_INCLUDED == TRUE)
#include "hidh_api.h"
#endif

#if (defined(MCA_INCLUDED) && MCA_INCLUDED == TRUE)
#include "mca_api.h"
#endif

#if (defined(BLE_INCLUDED) && BLE_INCLUDED == TRUE)
#include "gatt_api.h"
#if (defined(SMP_INCLUDED) && SMP_INCLUDED == TRUE)
#include "smp_api.h"
#endif
#endif

/***** BTA Modules ******/
#if BTA_INCLUDED == TRUE && BTA_DYNAMIC_MEMORY == TRUE
#include "bta_api.h"
#include "bta_sys.h"

#include "bta_ag_int.h"

#if BTA_HS_INCLUDED == TRUE
#include "bta_hs_int.h"
#endif

#include "bta_dm_int.h"

#if BTA_AR_INCLUDED==TRUE
#include "bta_ar_int.h"
#endif
#if BTA_AV_INCLUDED==TRUE
#include "bta_av_int.h"
#endif

#if BTA_HH_INCLUDED==TRUE
#include "bta_hh_int.h"
#endif

#if BTA_JV_INCLUDED==TRUE
#include "bta_jv_int.h"
tBTA_JV_CB *bta_jv_cb_ptr = NULL;
#endif

#if BTA_HL_INCLUDED == TRUE
#include "bta_hl_int.h"
#endif

#if BTA_GATT_INCLUDED == TRUE
#include "bta_gattc_int.h"
#include "bta_gatts_int.h"
#endif

#if BTA_PAN_INCLUDED==TRUE
#include "bta_pan_int.h"
#endif

#include "bta_sys_int.h"

/* control block for patch ram downloading */
#include "bta_prm_int.h"

#endif /* BTA_INCLUDED */

/*****************************************************************************
**                          F U N C T I O N S                                *
******************************************************************************/

/*****************************************************************************
**
** Function         BTE_InitStack
**
** Description      Initialize control block memory for each component.
**
**                  Note: The core stack components must be called
**                      before creating the BTU Task.  The rest of the
**                      components can be initialized at a later time if desired
**                      as long as the component's init function is called
**                      before accessing any of its functions.
**
** Returns          void
**
******************************************************************************/
void BTE_InitStack(void)
{
/* Initialize the optional stack components */
    RFCOMM_Init();

/**************************
** BNEP and its profiles **
***************************/
#if (defined(BNEP_INCLUDED) && BNEP_INCLUDED == TRUE)
    BNEP_Init();

#if (defined(PAN_INCLUDED) && PAN_INCLUDED == TRUE)
    PAN_Init();
#endif  /* PAN */
#endif  /* BNEP Included */


/**************************
** AVDT and its profiles **
***************************/
#if (defined(A2D_INCLUDED) && A2D_INCLUDED == TRUE)
    A2D_Init();
#endif  /* AADP */


    AVRC_Init();


/***********
** Others **
************/
    GAP_Init();

#if (defined(HID_HOST_INCLUDED) && HID_HOST_INCLUDED == TRUE)
    HID_HostInit();
#endif

#if (defined(MCA_INCLUDED) && MCA_INCLUDED == TRUE)
    MCA_Init();
#endif

/****************
** BTA Modules **
*****************/
#if (BTA_INCLUDED == TRUE && BTA_DYNAMIC_MEMORY == TRUE)
    memset((void*)bta_sys_cb_ptr, 0, sizeof(tBTA_SYS_CB));
    memset((void*)bta_dm_cb_ptr, 0, sizeof(tBTA_DM_CB));
    memset((void*)bta_dm_search_cb_ptr, 0, sizeof(tBTA_DM_SEARCH_CB));
    memset((void*)bta_dm_di_cb_ptr, 0, sizeof(tBTA_DM_DI_CB));
    memset((void*)bta_prm_cb_ptr, 0, sizeof(tBTA_PRM_CB));
    memset((void*)bta_ag_cb_ptr, 0, sizeof(tBTA_AG_CB));
#if BTA_HS_INCLUDED == TRUE
    memset((void*)bta_hs_cb_ptr, 0, sizeof(tBTA_HS_CB));
#endif
#if BTA_AR_INCLUDED==TRUE
    memset((void *)bta_ar_cb_ptr, 0, sizeof(tBTA_AR_CB));
#endif
#if BTA_AV_INCLUDED==TRUE
    memset((void *)bta_av_cb_ptr, 0, sizeof(tBTA_AV_CB));
#endif
#if BTA_HH_INCLUDED==TRUE
    memset((void *)bta_hh_cb_ptr, 0, sizeof(tBTA_HH_CB));
#endif
#if BTA_HL_INCLUDED==TRUE
    memset((void *)bta_hl_cb_ptr, 0, sizeof(tBTA_HL_CB));
#endif
#if BTA_GATT_INCLUDED==TRUE
    memset((void *)bta_gattc_cb_ptr, 0, sizeof(tBTA_GATTC_CB));
    memset((void *)bta_gatts_cb_ptr, 0, sizeof(tBTA_GATTS_CB));
#endif
#if BTA_PAN_INCLUDED==TRUE
    memset((void *)bta_pan_cb_ptr, 0, sizeof(tBTA_PAN_CB));
#endif

#endif /* BTA_INCLUDED == TRUE */

}
