/*
 * Copyright (c) 2011-2019 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */

/*
 * This file limUtils.cc contains the utility functions
 * LIM uses.
 * Author:        Chandra Modumudi
 * Date:          02/13/02
 * History:-
 * Date           Modified by    Modification Information
 * --------------------------------------------------------------------
 */

#include "schApi.h"
#include "limUtils.h"
#include "limTypes.h"
#include "limSecurityUtils.h"
#include "limPropExtsUtils.h"
#include "limSendMessages.h"
#include "limSerDesUtils.h"
#include "limAdmitControl.h"
#include "limStaHashApi.h"
#include "dot11f.h"
#include "dot11fdefs.h"
#include "wmmApsd.h"
#include "limTrace.h"
#ifdef FEATURE_WLAN_DIAG_SUPPORT
#include "vos_diag_core_event.h"
#endif //FEATURE_WLAN_DIAG_SUPPORT
#include "limIbssPeerMgmt.h"
#include "limSessionUtils.h"
#ifdef WLAN_FEATURE_VOWIFI_11R
#include "limFTDefs.h"
#endif
#include "limSession.h"
#include "vos_nvitem.h"

#include "pmmApi.h"
#ifdef WLAN_FEATURE_11W
#include "wni_cfg.h"
#endif

#ifdef SAP_AUTH_OFFLOAD
#include "limAssocUtils.h"
#endif

#include "nan_datapath.h"
/* Static global used to mark situations where pMac->lim.gLimTriggerBackgroundScanDuringQuietBss is SET
 * and limTriggerBackgroundScanDuringQuietBss() returned failure.  In this case, we will stop data
 * traffic instead of going into scan.  The recover function limProcessQuietBssTimeout() needs to have
 * this information. */
static tAniBool glimTriggerBackgroundScanDuringQuietBss_Status = eSIR_TRUE;

/* 11A Channel list to decode RX BD channel information */
static const tANI_U8 abChannel[]= {36,40,44,48,52,56,60,64,100,104,108,112,116,
            120,124,128,132,136,140,149,153,157,161,165
#ifdef FEATURE_WLAN_CH144
                ,144
#endif
};
#define abChannelSize (sizeof(abChannel)/  \
        sizeof(abChannel[0]))

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
static const tANI_U8 aUnsortedChannelList[]= {52,56,60,64,100,104,108,112,116,
            120,124,128,132,136,140,36,40,44,48,149,153,157,161,165
#ifdef FEATURE_WLAN_CH144
                ,144
#endif
};
#define aUnsortedChannelListSize (sizeof(aUnsortedChannelList)/  \
        sizeof(aUnsortedChannelList[0]))
#endif

#define SUCCESS 1

#define MAX_DTIM_PERIOD 15
#define MAX_DTIM_COUNT  15
#define DTIM_PERIOD_DEFAULT 1
#define DTIM_COUNT_DEFAULT  1

#define MAX_BA_WINDOW_SIZE_FOR_CISCO 25

/** -------------------------------------------------------------
\fn limAssignDialogueToken
\brief Assigns dialogue token.
\param     tpAniSirGlobal    pMac
\return tpDialogueToken - dialogueToken data structure.
  -------------------------------------------------------------*/

tpDialogueToken
limAssignDialogueToken(tpAniSirGlobal pMac)
{
    static tANI_U8 token;
    tpDialogueToken pCurrNode;
    pCurrNode = vos_mem_malloc(sizeof(tDialogueToken));
    if ( NULL == pCurrNode )
    {
        PELOGE(limLog(pMac, LOGE, FL("AllocateMemory failed"));)
        return NULL;
    }

    vos_mem_set((void *) pCurrNode, sizeof(tDialogueToken), 0);
    //first node in the list is being added.
    if(NULL == pMac->lim.pDialogueTokenHead)
    {
        pMac->lim.pDialogueTokenHead = pMac->lim.pDialogueTokenTail = pCurrNode;
    }
    else
    {
        pMac->lim.pDialogueTokenTail->next = pCurrNode;
        pMac->lim.pDialogueTokenTail = pCurrNode;
    }
    //assocId and tid of the node will be filled in by caller.
    pCurrNode->next = NULL;
    pCurrNode->token = token++;

    /* Dialog token should be a non-zero value */
    if (0 == pCurrNode->token)
       pCurrNode->token = token;

    PELOG4(limLog(pMac, LOG4, FL("token assigned = %d"), pCurrNode->token);)
    return pCurrNode;
}

/** -------------------------------------------------------------
\fn limSearchAndDeleteDialogueToken
\brief search dialogue token in the list and deletes it if found. returns failure if not found.
\param     tpAniSirGlobal    pMac
\param     tANI_U8 token
\param     tANI_U16 assocId
\param     tANI_U16 tid
\return eSirRetStatus - status of the search
  -------------------------------------------------------------*/


tSirRetStatus
limSearchAndDeleteDialogueToken(tpAniSirGlobal pMac, tANI_U8 token, tANI_U16 assocId, tANI_U16 tid)
{
    tpDialogueToken pCurrNode = pMac->lim.pDialogueTokenHead;
    tpDialogueToken pPrevNode = pMac->lim.pDialogueTokenHead;

    //if the list is empty
    if(NULL == pCurrNode)
      return eSIR_FAILURE;

    // if the matching node is the first node.
    if(pCurrNode &&
        (assocId == pCurrNode->assocId) &&
        (tid == pCurrNode->tid))
    {
        pMac->lim.pDialogueTokenHead = pCurrNode->next;
        //there was only one node in the list. So tail pointer also needs to be adjusted.
        if(NULL == pMac->lim.pDialogueTokenHead)
            pMac->lim.pDialogueTokenTail = NULL;
        vos_mem_free(pCurrNode);
        pMac->lim.pDialogueTokenHead = NULL;
        return eSIR_SUCCESS;
    }

    //first node did not match. so move to the next one.
    pCurrNode = pCurrNode->next;
    while(NULL != pCurrNode )
    {
        if(token == pCurrNode->token)
        {
            break;
        }

        pPrevNode = pCurrNode;
        pCurrNode = pCurrNode->next;
    }

    if(pCurrNode &&
        (assocId == pCurrNode->assocId) &&
        (tid == pCurrNode->tid))
    {
        pPrevNode->next = pCurrNode->next;
        //if the node being deleted is the last one then we also need to move the tail pointer to the prevNode.
        if(NULL == pCurrNode->next)
              pMac->lim.pDialogueTokenTail = pPrevNode;
        vos_mem_free(pCurrNode);
        pMac->lim.pDialogueTokenHead = NULL;
        return eSIR_SUCCESS;
    }

    PELOGW(limLog(pMac, LOGW, FL("LIM does not have matching dialogue token node"));)
    return eSIR_FAILURE;

}


/** -------------------------------------------------------------
\fn limDeleteDialogueTokenList
\brief deletes the complete lim dialogue token linked list.
\param     tpAniSirGlobal    pMac
\return     None
  -------------------------------------------------------------*/
void
limDeleteDialogueTokenList(tpAniSirGlobal pMac)
{
    tpDialogueToken pCurrNode = pMac->lim.pDialogueTokenHead;

    while(NULL != pMac->lim.pDialogueTokenHead)
    {
        pCurrNode = pMac->lim.pDialogueTokenHead;
        pMac->lim.pDialogueTokenHead = pMac->lim.pDialogueTokenHead->next;
        vos_mem_free(pCurrNode);
        pCurrNode = NULL;
    }
    pMac->lim.pDialogueTokenTail = NULL;
}

void
limGetBssidFromBD(tpAniSirGlobal pMac, tANI_U8 * pRxPacketInfo, tANI_U8 *bssId, tANI_U32 *pIgnore)
{
    tpSirMacDataHdr3a pMh = WDA_GET_RX_MPDUHEADER3A(pRxPacketInfo);
    *pIgnore = 0;

    if (pMh->fc.toDS == 1 && pMh->fc.fromDS == 0)
    {
        vos_mem_copy( bssId, pMh->addr1, 6);
        *pIgnore = 1;
    }
    else if (pMh->fc.toDS == 0 && pMh->fc.fromDS == 1)
    {
        vos_mem_copy ( bssId, pMh->addr2, 6);
        *pIgnore = 1;
    }
    else if (pMh->fc.toDS == 0 && pMh->fc.fromDS == 0)
    {
        vos_mem_copy( bssId, pMh->addr3, 6);
        *pIgnore = 0;
    }
    else
    {
        vos_mem_copy( bssId, pMh->addr1, 6);
        *pIgnore = 1;
    }
}


char *
limDot11ReasonStr(tANI_U16 reasonCode)
{
    switch (reasonCode)
    {
        case 0: return " ";
        CASE_RETURN_STRING(eSIR_MAC_UNSPEC_FAILURE_REASON);
        CASE_RETURN_STRING(eSIR_MAC_PREV_AUTH_NOT_VALID_REASON);
        CASE_RETURN_STRING(eSIR_MAC_DEAUTH_LEAVING_BSS_REASON);
        CASE_RETURN_STRING(eSIR_MAC_DISASSOC_DUE_TO_INACTIVITY_REASON);
        CASE_RETURN_STRING(eSIR_MAC_DISASSOC_DUE_TO_DISABILITY_REASON);
        CASE_RETURN_STRING(eSIR_MAC_CLASS2_FRAME_FROM_NON_AUTH_STA_REASON);
        CASE_RETURN_STRING(eSIR_MAC_CLASS3_FRAME_FROM_NON_ASSOC_STA_REASON);
        CASE_RETURN_STRING(eSIR_MAC_DISASSOC_LEAVING_BSS_REASON);
        CASE_RETURN_STRING(eSIR_MAC_STA_NOT_PRE_AUTHENTICATED_REASON);
        CASE_RETURN_STRING(eSIR_MAC_PWR_CAPABILITY_BAD_REASON);
        CASE_RETURN_STRING(eSIR_MAC_SPRTD_CHANNELS_BAD_REASON);

        CASE_RETURN_STRING(eSIR_MAC_INVALID_IE_REASON);
        CASE_RETURN_STRING(eSIR_MAC_MIC_FAILURE_REASON);
        CASE_RETURN_STRING(eSIR_MAC_4WAY_HANDSHAKE_TIMEOUT_REASON);
        CASE_RETURN_STRING(eSIR_MAC_GR_KEY_UPDATE_TIMEOUT_REASON);
        CASE_RETURN_STRING(eSIR_MAC_RSN_IE_MISMATCH_REASON);

        CASE_RETURN_STRING(eSIR_MAC_INVALID_MC_CIPHER_REASON);
        CASE_RETURN_STRING(eSIR_MAC_INVALID_UC_CIPHER_REASON);
        CASE_RETURN_STRING(eSIR_MAC_INVALID_AKMP_REASON);
        CASE_RETURN_STRING(eSIR_MAC_UNSUPPORTED_RSN_IE_VER_REASON);
        CASE_RETURN_STRING(eSIR_MAC_INVALID_RSN_CAPABILITIES_REASON);
        CASE_RETURN_STRING(eSIR_MAC_1X_AUTH_FAILURE_REASON);
        CASE_RETURN_STRING(eSIR_MAC_CIPHER_SUITE_REJECTED_REASON);
#ifdef FEATURE_WLAN_TDLS
        CASE_RETURN_STRING(eSIR_MAC_TDLS_TEARDOWN_PEER_UNREACHABLE);
        CASE_RETURN_STRING(eSIR_MAC_TDLS_TEARDOWN_UNSPEC_REASON);
#endif
        /* Reserved   27 - 30*/
#ifdef WLAN_FEATURE_11W
        CASE_RETURN_STRING(eSIR_MAC_ROBUST_MGMT_FRAMES_POLICY_VIOLATION);
#endif
        CASE_RETURN_STRING(eSIR_MAC_QOS_UNSPECIFIED_REASON);
        CASE_RETURN_STRING(eSIR_MAC_QAP_NO_BANDWIDTH_REASON);
        CASE_RETURN_STRING(eSIR_MAC_XS_UNACKED_FRAMES_REASON);
        CASE_RETURN_STRING(eSIR_MAC_BAD_TXOP_USE_REASON);
        CASE_RETURN_STRING(eSIR_MAC_PEER_STA_REQ_LEAVING_BSS_REASON);
        CASE_RETURN_STRING(eSIR_MAC_PEER_REJECT_MECHANISIM_REASON);
        CASE_RETURN_STRING(eSIR_MAC_MECHANISM_NOT_SETUP_REASON);

        CASE_RETURN_STRING(eSIR_MAC_PEER_TIMEDOUT_REASON);
        CASE_RETURN_STRING(eSIR_MAC_CIPHER_NOT_SUPPORTED_REASON);
        CASE_RETURN_STRING(eSIR_MAC_DISASSOC_DUE_TO_FTHANDOFF_REASON);
        /* Reserved 47 - 65535 */
        default:
            return "Unknown";
    }
}


char *
limMlmStateStr(tLimMlmStates state)
{
    switch (state)
    {
        case eLIM_MLM_OFFLINE_STATE:
            return "eLIM_MLM_OFFLINE_STATE";
        case eLIM_MLM_IDLE_STATE:
            return "eLIM_MLM_IDLE_STATE";
        case eLIM_MLM_WT_PROBE_RESP_STATE:
            return "eLIM_MLM_WT_PROBE_RESP_STATE";
        case eLIM_MLM_PASSIVE_SCAN_STATE:
            return "eLIM_MLM_PASSIVE_SCAN_STATE";
        case eLIM_MLM_WT_JOIN_BEACON_STATE:
            return "eLIM_MLM_WT_JOIN_BEACON_STATE";
        case eLIM_MLM_JOINED_STATE:
            return "eLIM_MLM_JOINED_STATE";
        case eLIM_MLM_BSS_STARTED_STATE:
            return "eLIM_MLM_BSS_STARTED_STATE";
        case eLIM_MLM_WT_AUTH_FRAME2_STATE:
            return "eLIM_MLM_WT_AUTH_FRAME2_STATE";
        case eLIM_MLM_WT_AUTH_FRAME3_STATE:
            return "eLIM_MLM_WT_AUTH_FRAME3_STATE";
        case eLIM_MLM_WT_AUTH_FRAME4_STATE:
            return "eLIM_MLM_WT_AUTH_FRAME4_STATE";
        case eLIM_MLM_AUTH_RSP_TIMEOUT_STATE:
            return "eLIM_MLM_AUTH_RSP_TIMEOUT_STATE";
        case eLIM_MLM_AUTHENTICATED_STATE:
            return "eLIM_MLM_AUTHENTICATED_STATE";
        case eLIM_MLM_WT_ASSOC_RSP_STATE:
            return "eLIM_MLM_WT_ASSOC_RSP_STATE";
        case eLIM_MLM_WT_REASSOC_RSP_STATE:
            return "eLIM_MLM_WT_REASSOC_RSP_STATE";
        case eLIM_MLM_WT_FT_REASSOC_RSP_STATE:
            return "eLIM_MLM_WT_FT_REASSOC_RSP_STATE";
        case eLIM_MLM_WT_DEL_STA_RSP_STATE:
            return "eLIM_MLM_WT_DEL_STA_RSP_STATE";
        case eLIM_MLM_WT_DEL_BSS_RSP_STATE:
            return "eLIM_MLM_WT_DEL_BSS_RSP_STATE";
        case eLIM_MLM_WT_ADD_STA_RSP_STATE:
            return "eLIM_MLM_WT_ADD_STA_RSP_STATE";
        case eLIM_MLM_WT_ADD_BSS_RSP_STATE:
            return "eLIM_MLM_WT_ADD_BSS_RSP_STATE";
        case eLIM_MLM_REASSOCIATED_STATE:
            return "eLIM_MLM_REASSOCIATED_STATE";
        case eLIM_MLM_LINK_ESTABLISHED_STATE:
            return "eLIM_MLM_LINK_ESTABLISHED_STATE";
        case eLIM_MLM_WT_ASSOC_CNF_STATE:
            return "eLIM_MLM_WT_ASSOC_CNF_STATE";
        case eLIM_MLM_WT_ADD_BSS_RSP_ASSOC_STATE:
            return "eLIM_MLM_WT_ADD_BSS_RSP_ASSOC_STATE";
        case eLIM_MLM_WT_ADD_BSS_RSP_REASSOC_STATE:
            return "eLIM_MLM_WT_ADD_BSS_RSP_REASSOC_STATE";
        case eLIM_MLM_WT_ADD_BSS_RSP_FT_REASSOC_STATE:
            return "eLIM_MLM_WT_ADD_BSS_RSP_FT_REASSOC_STATE";
        case eLIM_MLM_WT_ASSOC_DEL_STA_RSP_STATE:
            return "eLIM_MLM_WT_ASSOC_DEL_STA_RSP_STATE";
        case eLIM_MLM_WT_SET_BSS_KEY_STATE:
            return "eLIM_MLM_WT_SET_BSS_KEY_STATE";
        case eLIM_MLM_WT_SET_STA_KEY_STATE:
            return "eLIM_MLM_WT_SET_STA_KEY_STATE";
        default:
            return "INVALID MLM state";
    }
}

void
limPrintMlmState(tpAniSirGlobal pMac, tANI_U16 logLevel, tLimMlmStates state)
{
    limLog(pMac, logLevel, limMlmStateStr(state));
}

char* limBssTypeStr(tSirBssType bssType)
{
    switch(bssType)
    {
        case eSIR_INFRASTRUCTURE_MODE:
            return "eSIR_INFRASTRUCTURE_MODE";
        case eSIR_IBSS_MODE:
            return "eSIR_IBSS_MODE";
        case eSIR_BTAMP_STA_MODE:
            return "eSIR_BTAMP_STA_MODE";
        case eSIR_BTAMP_AP_MODE:
            return "eSIR_BTAMP_AP_MODE";
        case eSIR_AUTO_MODE:
            return "eSIR_AUTO_MODE";
        default:
            return "Invalid BSS Type";
    }
}

char *limResultCodeStr(tSirResultCodes resultCode)
{
    switch (resultCode)
    {
      case eSIR_SME_SUCCESS:
            return "eSIR_SME_SUCCESS";
      case eSIR_EOF_SOF_EXCEPTION:
            return "eSIR_EOF_SOF_EXCEPTION";
      case eSIR_BMU_EXCEPTION:
            return "eSIR_BMU_EXCEPTION";
      case eSIR_LOW_PDU_EXCEPTION:
            return "eSIR_LOW_PDU_EXCEPTION";
      case eSIR_USER_TRIG_RESET:
            return"eSIR_USER_TRIG_RESET";
      case eSIR_LOGP_EXCEPTION:
            return "eSIR_LOGP_EXCEPTION";
      case eSIR_CP_EXCEPTION:
            return "eSIR_CP_EXCEPTION";
      case eSIR_STOP_BSS:
            return "eSIR_STOP_BSS";
      case eSIR_AHB_HANG_EXCEPTION:
            return "eSIR_AHB_HANG_EXCEPTION";
      case eSIR_DPU_EXCEPTION:
            return "eSIR_DPU_EXCEPTION";
      case eSIR_RXP_EXCEPTION:
            return "eSIR_RXP_EXCEPTION";
      case eSIR_MCPU_EXCEPTION:
            return "eSIR_MCPU_EXCEPTION";
      case eSIR_MCU_EXCEPTION:
            return "eSIR_MCU_EXCEPTION";
      case eSIR_MTU_EXCEPTION:
            return "eSIR_MTU_EXCEPTION";
      case eSIR_MIF_EXCEPTION:
            return "eSIR_MIF_EXCEPTION";
      case eSIR_FW_EXCEPTION:
            return "eSIR_FW_EXCEPTION";
      case eSIR_MAILBOX_SANITY_CHK_FAILED:
            return "eSIR_MAILBOX_SANITY_CHK_FAILED";
      case eSIR_RADIO_HW_SWITCH_STATUS_IS_OFF:
            return "eSIR_RADIO_HW_SWITCH_STATUS_IS_OFF";
      case eSIR_CFB_FLAG_STUCK_EXCEPTION:
            return "eSIR_CFB_FLAG_STUCK_EXCEPTION";
      case eSIR_SME_BASIC_RATES_NOT_SUPPORTED_STATUS:
            return "eSIR_SME_BASIC_RATES_NOT_SUPPORTED_STATUS";
      case eSIR_SME_INVALID_PARAMETERS:
            return "eSIR_SME_INVALID_PARAMETERS";
      case eSIR_SME_UNEXPECTED_REQ_RESULT_CODE:
            return "eSIR_SME_UNEXPECTED_REQ_RESULT_CODE";
      case eSIR_SME_RESOURCES_UNAVAILABLE:
            return "eSIR_SME_RESOURCES_UNAVAILABLE";
      case eSIR_SME_SCAN_FAILED:
            return "eSIR_SME_SCAN_FAILED";
      case eSIR_SME_BSS_ALREADY_STARTED_OR_JOINED:
            return "eSIR_SME_BSS_ALREADY_STARTED_OR_JOINED";
      case eSIR_SME_LOST_LINK_WITH_PEER_RESULT_CODE:
            return "eSIR_SME_LOST_LINK_WITH_PEER_RESULT_CODE";
      case eSIR_SME_REFUSED:
            return "eSIR_SME_REFUSED";
      case eSIR_SME_JOIN_TIMEOUT_RESULT_CODE:
            return "eSIR_SME_JOIN_TIMEOUT_RESULT_CODE";
      case eSIR_SME_AUTH_TIMEOUT_RESULT_CODE:
            return "eSIR_SME_AUTH_TIMEOUT_RESULT_CODE";
      case eSIR_SME_ASSOC_TIMEOUT_RESULT_CODE:
            return "eSIR_SME_ASSOC_TIMEOUT_RESULT_CODE";
      case eSIR_SME_REASSOC_TIMEOUT_RESULT_CODE:
            return "eSIR_SME_REASSOC_TIMEOUT_RESULT_CODE";
      case eSIR_SME_MAX_NUM_OF_PRE_AUTH_REACHED:
            return "eSIR_SME_MAX_NUM_OF_PRE_AUTH_REACHED";
      case eSIR_SME_AUTH_REFUSED:
            return "eSIR_SME_AUTH_REFUSED";
      case eSIR_SME_INVALID_WEP_DEFAULT_KEY:
            return "eSIR_SME_INVALID_WEP_DEFAULT_KEY";
      case eSIR_SME_ASSOC_REFUSED:
            return "eSIR_SME_ASSOC_REFUSED";
      case eSIR_SME_REASSOC_REFUSED:
            return "eSIR_SME_REASSOC_REFUSED";
      case eSIR_SME_STA_NOT_AUTHENTICATED:
            return "eSIR_SME_STA_NOT_AUTHENTICATED";
      case eSIR_SME_STA_NOT_ASSOCIATED:
            return "eSIR_SME_STA_NOT_ASSOCIATED";
      case eSIR_SME_STA_DISASSOCIATED:
            return "eSIR_SME_STA_DISASSOCIATED";
      case eSIR_SME_ALREADY_JOINED_A_BSS:
            return "eSIR_SME_ALREADY_JOINED_A_BSS";
      case eSIR_ULA_COMPLETED:
            return "eSIR_ULA_COMPLETED";
      case eSIR_ULA_FAILURE:
            return "eSIR_ULA_FAILURE";
      case eSIR_SME_LINK_ESTABLISHED:
            return "eSIR_SME_LINK_ESTABLISHED";
      case eSIR_SME_UNABLE_TO_PERFORM_MEASUREMENTS:
            return "eSIR_SME_UNABLE_TO_PERFORM_MEASUREMENTS";
      case eSIR_SME_UNABLE_TO_PERFORM_DFS:
            return "eSIR_SME_UNABLE_TO_PERFORM_DFS";
      case eSIR_SME_DFS_FAILED:
            return "eSIR_SME_DFS_FAILED";
      case eSIR_SME_TRANSFER_STA:
            return "eSIR_SME_TRANSFER_STA";
      case eSIR_SME_INVALID_LINK_TEST_PARAMETERS:
            return "eSIR_SME_INVALID_LINK_TEST_PARAMETERS";
      case eSIR_SME_LINK_TEST_MAX_EXCEEDED:
            return "eSIR_SME_LINK_TEST_MAX_EXCEEDED";
      case eSIR_SME_UNSUPPORTED_RATE:
            return "eSIR_SME_UNSUPPORTED_RATE";
      case eSIR_SME_LINK_TEST_TIMEOUT:
            return "eSIR_SME_LINK_TEST_TIMEOUT";
      case eSIR_SME_LINK_TEST_COMPLETE:
            return "eSIR_SME_LINK_TEST_COMPLETE";
      case eSIR_SME_LINK_TEST_INVALID_STATE:
            return "eSIR_SME_LINK_TEST_INVALID_STATE";
      case eSIR_SME_LINK_TEST_INVALID_ADDRESS:
            return "eSIR_SME_LINK_TEST_INVALID_ADDRESS";
      case eSIR_SME_SETCONTEXT_FAILED:
            return "eSIR_SME_SETCONTEXT_FAILED";
      case eSIR_SME_BSS_RESTART:
            return "eSIR_SME_BSS_RESTART";
      case eSIR_SME_MORE_SCAN_RESULTS_FOLLOW:
            return "eSIR_SME_MORE_SCAN_RESULTS_FOLLOW";
      case eSIR_SME_INVALID_ASSOC_RSP_RXED:
            return "eSIR_SME_INVALID_ASSOC_RSP_RXED";
      case eSIR_SME_MIC_COUNTER_MEASURES:
            return "eSIR_SME_MIC_COUNTER_MEASURES";
      case eSIR_SME_ADDTS_RSP_TIMEOUT:
            return "eSIR_SME_ADDTS_RSP_TIMEOUT";
      case eSIR_SME_RECEIVED:
            return "eSIR_SME_RECEIVED";
      case eSIR_SME_CHANNEL_SWITCH_FAIL:
            return "eSIR_SME_CHANNEL_SWITCH_FAIL";
      case eSIR_SME_CHANNEL_SWITCH_DISABLED:
            return "eSIR_SME_CHANNEL_SWITCH_DISABLED";
      case eSIR_SME_HAL_SCAN_INIT_FAILED:
            return "eSIR_SME_HAL_SCAN_INIT_FAILED";
      case eSIR_SME_HAL_SCAN_START_FAILED:
            return "eSIR_SME_HAL_SCAN_START_FAILED";
      case eSIR_SME_HAL_SCAN_END_FAILED:
            return "eSIR_SME_HAL_SCAN_END_FAILED";
      case eSIR_SME_HAL_SCAN_FINISH_FAILED:
            return "eSIR_SME_HAL_SCAN_FINISH_FAILED";
      case eSIR_SME_HAL_SEND_MESSAGE_FAIL:
            return "eSIR_SME_HAL_SEND_MESSAGE_FAIL";

        default:
            return "INVALID resultCode";
    }
}

/**
 * limInitMlm() - This function is called by limProcessSmeMessages()
 * to initialize MLM state machine on STA
 *
 * @pMac:   Pointer to Global MAC structure
 *
 * @Return: Status of operation
 */
tSirRetStatus
limInitMlm(tpAniSirGlobal pMac)
{
    tANI_U32 retVal;

    pMac->lim.gLimTimersCreated = 0;

    MTRACE(macTrace(pMac, TRACE_CODE_MLM_STATE, NO_SESSION, pMac->lim.gLimMlmState));

    /// Initialize scan result hash table
    limReInitScanResults(pMac); //sep26th review

#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    /// Initialize lfr scan result hash table
    // Could there be a problem in multisession with SAP/P2P GO, when in the
    // middle of FW bg scan, SAP started; Again that could be a problem even on
    // infra + SAP/P2P GO too - TBD
    limReInitLfrScanResults(pMac);
#endif

    /// Initialize number of pre-auth contexts
    pMac->lim.gLimNumPreAuthContexts = 0;

    /// Initialize MAC based Authentication STA list
    limInitPreAuthList(pMac);

    // Create timers used by LIM
    retVal = limCreateTimers(pMac);
    if(retVal != TX_SUCCESS) {
        limLog(pMac, LOGP, FL(" limCreateTimers Failed to create lim timers "));
        return eSIR_FAILURE;
    }

    pMac->lim.gLimTimersCreated = 1;
    return eSIR_SUCCESS;
} /*** end limInitMlm() ***/



/**
 * limCleanupMlm()
 *
 *FUNCTION:
 * This function is called to cleanup any resources
 * allocated by the  MLM state machine.
 *
 *PARAMS:
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * It is assumed that BSS is already informed that we're leaving it
 * before this function is called.
 *
 * @param  pMac      Pointer to Global MAC structure
 * @param  None
 * @return None
 */
void
limCleanupMlm(tpAniSirGlobal pMac)
{
    tANI_U32   n;
    tLimPreAuthNode **pAuthNode;
#ifdef WLAN_FEATURE_11W
    tANI_U32  bss_entry, sta_entry;
    tpDphHashNode pStaDs = NULL;
    tpPESession psessionEntry = NULL;
#endif

    if (pMac->lim.gLimTimersCreated == 1)
    {
        // Deactivate and delete MIN/MAX channel timers.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimMinChannelTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimMinChannelTimer);
        tx_timer_deactivate(&pMac->lim.limTimers.gLimMaxChannelTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimMaxChannelTimer);
        tx_timer_deactivate(&pMac->lim.limTimers.gLimPeriodicProbeReqTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimPeriodicProbeReqTimer);


        // Deactivate and delete channel switch timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimChannelSwitchTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimChannelSwitchTimer);


        // Deactivate and delete addts response timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimAddtsRspTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimAddtsRspTimer);

        // Deactivate and delete Join failure timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimJoinFailureTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimJoinFailureTimer);

        // Deactivate and delete Periodic Join Probe Request timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimPeriodicJoinProbeReqTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimPeriodicJoinProbeReqTimer);

        // Deactivate and delete Auth Retry timer.
        tx_timer_deactivate(&pMac->lim.limTimers.g_lim_periodic_auth_retry_timer);
        tx_timer_delete(&pMac->lim.limTimers.g_lim_periodic_auth_retry_timer);

        // Deactivate and delete Association failure timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimAssocFailureTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimAssocFailureTimer);

        // Deactivate and delete Reassociation failure timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimReassocFailureTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimReassocFailureTimer);

        // Deactivate and delete Authentication failure timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimAuthFailureTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimAuthFailureTimer);

        // Deactivate and delete Heartbeat timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimHeartBeatTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimHeartBeatTimer);

        // Deactivate and delete wait-for-probe-after-Heartbeat timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimProbeAfterHBTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimProbeAfterHBTimer);

        // Deactivate and delete Quiet timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimQuietTimer);

        // Deactivate and delete Quiet BSS timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietBssTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimQuietBssTimer);

        // Deactivate and delete LIM background scan timer.
        tx_timer_deactivate(&pMac->lim.limTimers.gLimBackgroundScanTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimBackgroundScanTimer);


        // Deactivate and delete cnf wait timer
        for (n = 0; n < (pMac->lim.maxStation + 1); n++)
        {
            tx_timer_deactivate(&pMac->lim.limTimers.gpLimCnfWaitTimer[n]);
            tx_timer_delete(&pMac->lim.limTimers.gpLimCnfWaitTimer[n]);
        }

        // Deactivate and delete keepalive timer
        tx_timer_deactivate(&pMac->lim.limTimers.gLimKeepaliveTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimKeepaliveTimer);

        pAuthNode = pMac->lim.gLimPreAuthTimerTable.pTable;

        //Deactivate any Authentication response timers
        limDeletePreAuthList(pMac);

        for (n = 0; n < pMac->lim.gLimPreAuthTimerTable.numEntry; n++)
        {
            // Delete any Authentication response
            // timers, which might have been started.
            tx_timer_delete(&pAuthNode[n]->timer);
        }

        tx_timer_deactivate(&pMac->lim.limTimers.gLimUpdateOlbcCacheTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimUpdateOlbcCacheTimer);
        tx_timer_deactivate(&pMac->lim.limTimers.gLimPreAuthClnupTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimPreAuthClnupTimer);

#ifdef WLAN_FEATURE_VOWIFI_11R
        // Deactivate and delete FT Preauth response timer
        tx_timer_deactivate(&pMac->lim.limTimers.gLimFTPreAuthRspTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimFTPreAuthRspTimer);
#endif

        // Deactivate and delete remain on channel timer
        tx_timer_deactivate(&pMac->lim.limTimers.gLimRemainOnChannelTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimRemainOnChannelTimer);

#if defined(FEATURE_WLAN_ESE) && !defined(FEATURE_WLAN_ESE_UPLOAD)
        // Deactivate and delete TSM
        tx_timer_deactivate(&pMac->lim.limTimers.gLimEseTsmTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimEseTsmTimer);
#endif /* FEATURE_WLAN_ESE && !FEATURE_WLAN_ESE_UPLOAD */

        tx_timer_deactivate(&pMac->lim.limTimers.gLimDisassocAckTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimDisassocAckTimer);

        tx_timer_deactivate(&pMac->lim.limTimers.gLimDeauthAckTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimDeauthAckTimer);

        tx_timer_deactivate(&pMac->lim.limTimers.gLimP2pSingleShotNoaInsertTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimP2pSingleShotNoaInsertTimer);

        tx_timer_deactivate(&pMac->lim.limTimers.gLimActiveToPassiveChannelTimer);
        tx_timer_delete(&pMac->lim.limTimers.gLimActiveToPassiveChannelTimer);

        tx_timer_deactivate(&pMac->lim.limTimers.sae_auth_timer);
        tx_timer_delete(&pMac->lim.limTimers.sae_auth_timer);

        pMac->lim.gLimTimersCreated = 0;
    }

#ifdef WLAN_FEATURE_11W
    /*
     * When SSR is triggered, we need to loop through
     * each STA associated per BSSId and deactivate/delete
     * the pmfSaQueryTimer for it
     */
    for (bss_entry = 0; bss_entry < pMac->lim.maxBssId; bss_entry++)
    {
         if (pMac->lim.gpSession[bss_entry].valid)
         {
             for (sta_entry = 1; sta_entry < pMac->lim.gLimAssocStaLimit;
                  sta_entry++)
             {
                  psessionEntry = &pMac->lim.gpSession[bss_entry];
                  pStaDs = dphGetHashEntry(pMac, sta_entry,
                                          &psessionEntry->dph.dphHashTable);
                  if (NULL == pStaDs)
                  {
                      continue;
                  }
                  VOS_TRACE(VOS_MODULE_ID_PE, VOS_TRACE_LEVEL_ERROR,
                            FL("Deleting pmfSaQueryTimer for staid[%d]"),
                            pStaDs->staIndex) ;
                  tx_timer_deactivate(&pStaDs->pmfSaQueryTimer);
                  tx_timer_delete(&pStaDs->pmfSaQueryTimer);
            }
        }
    }
#endif

    /// Cleanup cached scan list
    limReInitScanResults(pMac);
#ifdef WLAN_FEATURE_ROAM_SCAN_OFFLOAD
    /// Cleanup cached scan list
    limReInitLfrScanResults(pMac);
#endif

} /*** end limCleanupMlm() ***/



/**
 * limCleanupLmm()
 *
 *FUNCTION:
 * This function is called to cleanup any resources
 * allocated by LMM sub-module.
 *
 *PARAMS:
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param  pMac      Pointer to Global MAC structure
 * @return None
 */

void
limCleanupLmm(tpAniSirGlobal pMac)
{
} /*** end limCleanupLmm() ***/



/**
 * limIsAddrBC()
 *
 *FUNCTION:
 * This function is called in various places within LIM code
 * to determine whether passed MAC address is a broadcast or not
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param macAddr  Indicates MAC address that need to be determined
 *                 whether it is Broadcast address or not
 *
 * @return true if passed address is Broadcast address else false
 */

tANI_U8
limIsAddrBC(tSirMacAddr macAddr)
{
    int i;
    for (i = 0; i < 6; i++)
    {
        if ((macAddr[i] & 0xFF) != 0xFF)
            return false;
    }

    return true;
} /****** end limIsAddrBC() ******/



/**
 * limIsGroupAddr()
 *
 *FUNCTION:
 * This function is called in various places within LIM code
 * to determine whether passed MAC address is a group address or not
 *
 *LOGIC:
 * If least significant bit of first octet of the MAC address is
 * set to 1, it is a Group address.
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param macAddr  Indicates MAC address that need to be determined
 *                 whether it is Group address or not
 *
 * @return true if passed address is Group address else false
 */

tANI_U8
limIsGroupAddr(tSirMacAddr macAddr)
{
    if ((macAddr[0] & 0x01) == 0x01)
        return true;
    else
        return false;
} /****** end limIsGroupAddr() ******/

/**
 * limPostMsgApiNoWait()
 *
 *FUNCTION:
 * This function is called from other thread while posting a
 * message to LIM message Queue gSirLimMsgQ with NO_WAIT option
 *
 *LOGIC:
 * NA
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param  pMsg - Pointer to the Global MAC structure
 * @param  pMsg - Pointer to the message structure
 * @return None
 */

tANI_U32
limPostMsgApiNoWait(tpAniSirGlobal pMac, tSirMsgQ *pMsg)
{
    limProcessMessages(pMac, pMsg);
    return TX_SUCCESS;
} /*** end limPostMsgApiNoWait() ***/



/**
 * limPrintMacAddr()
 *
 *FUNCTION:
 * This function is called to print passed MAC address
 * in : format.
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * @param  macAddr  - MacAddr to be printed
 * @param  logLevel - Loglevel to be used
 *
 * @return None.
 */

void
limPrintMacAddr(tpAniSirGlobal pMac, tSirMacAddr macAddr, tANI_U8 logLevel)
{
    limLog(pMac, logLevel,
           FL(MAC_ADDRESS_STR), MAC_ADDR_ARRAY(macAddr));
} /****** end limPrintMacAddr() ******/






/*
 * limResetDeferredMsgQ()
 *
 *FUNCTION:
 * This function resets the deferred message queue parameters.
 *
 *PARAMS:
 * @param pMac     - Pointer to Global MAC structure
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 *RETURNS:
 * None
 */

void limResetDeferredMsgQ(tpAniSirGlobal pMac)
{
    pMac->lim.gLimDeferredMsgQ.size =
    pMac->lim.gLimDeferredMsgQ.write =
    pMac->lim.gLimDeferredMsgQ.read = 0;

}


#define LIM_DEFERRED_Q_CHECK_THRESHOLD  (MAX_DEFERRED_QUEUE_LEN/2)
#define LIM_MAX_NUM_MGMT_FRAME_DEFERRED (MAX_DEFERRED_QUEUE_LEN/2)

/*
 * limWriteDeferredMsgQ()
 *
 *FUNCTION:
 * This function queues up a deferred message for later processing on the
 * STA side.
 *
 *PARAMS:
 * @param pMac     - Pointer to Global MAC structure
 * @param limMsg   - a LIM message
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 *RETURNS:
 * None
 */

tANI_U8 limWriteDeferredMsgQ(tpAniSirGlobal pMac, tpSirMsgQ limMsg)
{
    PELOG1(limLog(pMac, LOG1,
           FL("**  Queue a deferred message (size %d, write %d) - type 0x%x  **"),
           pMac->lim.gLimDeferredMsgQ.size, pMac->lim.gLimDeferredMsgQ.write,
           limMsg->type);)

        /*
         ** check if the deferred message queue is full
         **/
    if (pMac->lim.gLimDeferredMsgQ.size >= MAX_DEFERRED_QUEUE_LEN)
    {
        if(!(pMac->lim.deferredMsgCnt & 0xF))
        {
            limLog(pMac, LOGE,
               FL("Deferred Message Queue is full. Msg:%d Messages Failed:%d"),
               limMsg->type, ++pMac->lim.deferredMsgCnt);
            vos_flush_logs(WLAN_LOG_TYPE_NON_FATAL,
                           WLAN_LOG_INDICATOR_HOST_DRIVER,
                           WLAN_LOG_REASON_QUEUE_FULL,
                           DUMP_VOS_TRACE);
        }
        else
        {
            pMac->lim.deferredMsgCnt++;
        }
        return TX_QUEUE_FULL;
    }

    /*
    ** In the application, there should not be more than 1 message get
    ** queued up. If happens, flags a warning. In the future, this can
    ** happen.
    **/
    if (pMac->lim.gLimDeferredMsgQ.size > 0)
    {
        PELOGW(limLog(pMac, LOGW, FL("%d Deferred messages (type 0x%x, scan %d, global sme %d, global mlme %d, addts %d)"),
               pMac->lim.gLimDeferredMsgQ.size, limMsg->type,
               limIsSystemInScanState(pMac),
               pMac->lim.gLimSmeState, pMac->lim.gLimMlmState,
               pMac->lim.gLimAddtsSent);)
    }

    /*
    ** To prevent the deferred Q is full of management frames, only give them certain space
    **/
    if( SIR_BB_XPORT_MGMT_MSG == limMsg->type )
    {
        if( LIM_DEFERRED_Q_CHECK_THRESHOLD < pMac->lim.gLimDeferredMsgQ.size )
        {
            tANI_U16 idx, count = 0;
            for(idx = 0; idx < pMac->lim.gLimDeferredMsgQ.size; idx++)
            {
                if( SIR_BB_XPORT_MGMT_MSG == pMac->lim.gLimDeferredMsgQ.deferredQueue[idx].type )
                {
                    count++;
                }
            }
            if( LIM_MAX_NUM_MGMT_FRAME_DEFERRED < count )
            {
                //We reach the quota for management frames, drop this one
                PELOGW(limLog(pMac, LOGW, FL("Cannot deferred. Msg: %d Too many (count=%d) already"), limMsg->type, count);)
                //Return error, caller knows what to do
                return TX_QUEUE_FULL;
            }
        }
    }

    ++pMac->lim.gLimDeferredMsgQ.size;

    /* reset the count here since we are able to defer the message */
    if(pMac->lim.deferredMsgCnt != 0)
    {
        pMac->lim.deferredMsgCnt = 0;
    }

    /*
    ** if the write pointer hits the end of the queue, rewind it
    **/
    if (pMac->lim.gLimDeferredMsgQ.write >= MAX_DEFERRED_QUEUE_LEN)
        pMac->lim.gLimDeferredMsgQ.write = 0;

    /*
    ** save the message to the queue and advanced the write pointer
    **/
    vos_mem_copy( (tANI_U8 *)&pMac->lim.gLimDeferredMsgQ.deferredQueue[
                    pMac->lim.gLimDeferredMsgQ.write++],
                  (tANI_U8 *)limMsg,
                  sizeof(tSirMsgQ));
    return TX_SUCCESS;

}

/*
 * limReadDeferredMsgQ()
 *
 *FUNCTION:
 * This function dequeues a deferred message for processing on the
 * STA side.
 *
 *PARAMS:
 * @param pMac     - Pointer to Global MAC structure
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 *
 *
 *RETURNS:
 * Returns the message at the head of the deferred message queue
 */

tSirMsgQ* limReadDeferredMsgQ(tpAniSirGlobal pMac)
{
    tSirMsgQ    *msg;

    /*
    ** check any messages left. If no, return
    **/
    if (pMac->lim.gLimDeferredMsgQ.size <= 0)
        return NULL;

    /*
    ** decrement the queue size
    **/
    pMac->lim.gLimDeferredMsgQ.size--;

    /*
    ** retrieve the message from the head of the queue
    **/
    msg = &pMac->lim.gLimDeferredMsgQ.deferredQueue[pMac->lim.gLimDeferredMsgQ.read];

    /*
    ** advance the read pointer
    **/
    pMac->lim.gLimDeferredMsgQ.read++;

    /*
    ** if the read pointer hits the end of the queue, rewind it
    **/
    if (pMac->lim.gLimDeferredMsgQ.read >= MAX_DEFERRED_QUEUE_LEN)
        pMac->lim.gLimDeferredMsgQ.read = 0;

   PELOG1(limLog(pMac, LOG1,
           FL("**  DeQueue a deferred message (size %d read %d) - type 0x%x  **"),
           pMac->lim.gLimDeferredMsgQ.size, pMac->lim.gLimDeferredMsgQ.read,
           msg->type);)

   PELOG1(limLog(pMac, LOG1, FL("DQ msg -- scan %d, global sme %d, global mlme %d, addts %d"),
           limIsSystemInScanState(pMac),
           pMac->lim.gLimSmeState, pMac->lim.gLimMlmState,
           pMac->lim.gLimAddtsSent);)

    return(msg);
}

tSirRetStatus
limSysProcessMmhMsgApi(tpAniSirGlobal pMac,
                    tSirMsgQ *pMsg,
                    tANI_U8 qType)
{
// FIXME
   SysProcessMmhMsg(pMac, pMsg);
   return eSIR_SUCCESS;
}

void limHandleUpdateOlbcCache(tpAniSirGlobal pMac)
{
    int i;
    static int enable;
    tUpdateBeaconParams beaconParams;

    tpPESession       psessionEntry = limIsApSessionActive(pMac);

    if (psessionEntry == NULL)
    {
        PELOGE(limLog(pMac, LOGE, FL(" Session not found"));)
        return;
    }

    vos_mem_set( ( tANI_U8* )&beaconParams, sizeof( tUpdateBeaconParams), 0);
    beaconParams.bssIdx = psessionEntry->bssIdx;

    beaconParams.paramChangeBitmap = 0;
    /*
    ** This is doing a 2 pass check. The first pass is to invalidate
    ** all the cache entries. The second pass is to decide whether to
    ** disable protection.
    **/
    if (!enable)
    {

            PELOG2(limLog(pMac, LOG2, FL("Resetting OLBC cache"));)
            psessionEntry->gLimOlbcParams.numSta = 0;
            psessionEntry->gLimOverlap11gParams.numSta = 0;
            psessionEntry->gLimOverlapHt20Params.numSta = 0;
            psessionEntry->gLimNonGfParams.numSta = 0;
            psessionEntry->gLimLsigTxopParams.numSta = 0;

        for (i=0; i < LIM_PROT_STA_OVERLAP_CACHE_SIZE; i++)
            pMac->lim.protStaOverlapCache[i].active = false;

        enable = 1;
    }
    else
    {

        if (!psessionEntry->gLimOlbcParams.numSta)
        {
            if (psessionEntry->gLimOlbcParams.protectionEnabled)
            {
                if (!psessionEntry->gLim11bParams.protectionEnabled)
                {
                    PELOG1(limLog(pMac, LOG1, FL("Overlap cache all clear and no 11B STA detected"));)
                    limEnable11gProtection(pMac, false, true, &beaconParams, psessionEntry);
                }
            }
        }

        if (!psessionEntry->gLimOverlap11gParams.numSta)
        {
            if (psessionEntry->gLimOverlap11gParams.protectionEnabled)
            {
                if (!psessionEntry->gLim11gParams.protectionEnabled)
                {
                    PELOG1(limLog(pMac, LOG1, FL("Overlap cache all clear and no 11G STA detected"));)
                    limEnableHtProtectionFrom11g(pMac, false, true, &beaconParams,psessionEntry);
                }
            }
        }

        if (!psessionEntry->gLimOverlapHt20Params.numSta)
        {
            if (psessionEntry->gLimOverlapHt20Params.protectionEnabled)
            {
                if (!psessionEntry->gLimHt20Params.protectionEnabled)
                {
                    PELOG1(limLog(pMac, LOG1, FL("Overlap cache all clear and no HT20 STA detected"));)
                    limEnable11gProtection(pMac, false, true, &beaconParams,psessionEntry);
                }
            }
        }

        enable = 0;
    }

    if ((VOS_FALSE == pMac->sap.SapDfsInfo.is_dfs_cac_timer_running)
        && beaconParams.paramChangeBitmap)
    {
        schSetFixedBeaconFields(pMac,psessionEntry);
        limSendBeaconParams(pMac, &beaconParams, psessionEntry);
    }

    // Start OLBC timer
    if (tx_timer_activate(&pMac->lim.limTimers.gLimUpdateOlbcCacheTimer) != TX_SUCCESS)
    {
        limLog(pMac, LOGE, FL("tx_timer_activate failed"));
    }
}

/**
 * limIsNullSsid()
 *
 *FUNCTION:
 * This function checks if Ssid supplied is Null SSID
 *
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param tSirMacSSid *
 *
 *
 * @return true if SSID is Null SSID else false
 */

tANI_U8
limIsNullSsid( tSirMacSSid *pSsid )
{
    tANI_U8 fNullSsid = false;
    tANI_U32 SsidLength;
    tANI_U8 *pSsidStr;

    do
    {
        if ( 0 == pSsid->length )
        {
            fNullSsid = true;
            break;
        }

#define ASCII_SPACE_CHARACTER 0x20
        /* If the first charactes is space and SSID length is 1
         * then consider it as NULL SSID*/
        if((ASCII_SPACE_CHARACTER == pSsid->ssId[0])&&
           (pSsid->length == 1))
        {
             fNullSsid = true;
             break;
        }
        else
        {
            /* check if all the charactes in SSID are NULL*/
            SsidLength = pSsid->length;
            pSsidStr = pSsid->ssId;

            while ( SsidLength )
            {
                if( *pSsidStr )
                    break;

                pSsidStr++;
                SsidLength--;
            }

            if( 0 == SsidLength )
            {
                fNullSsid = true;
                break;
            }
        }
    }
    while( 0 );

    return fNullSsid;
} /****** end limIsNullSsid() ******/




/** -------------------------------------------------------------
\fn limUpdateProtStaParams
\brief updates protection related counters.
\param      tpAniSirGlobal    pMac
\param      tSirMacAddr peerMacAddr
\param      tLimProtStaCacheType protStaCacheType
\param      tHalBitVal gfSupported
\param      tHalBitVal lsigTxopSupported
\return      None
  -------------------------------------------------------------*/
void
limUpdateProtStaParams(tpAniSirGlobal pMac,
tSirMacAddr peerMacAddr, tLimProtStaCacheType protStaCacheType,
tHalBitVal gfSupported, tHalBitVal lsigTxopSupported,
tpPESession psessionEntry)
{
  tANI_U32 i;

  PELOG1(limLog(pMac,LOG1, FL("A STA is associated:"));
  limLog(pMac,LOG1, FL("Addr : "));
  limPrintMacAddr(pMac, peerMacAddr, LOG1);)

  for (i=0; i<LIM_PROT_STA_CACHE_SIZE; i++)
  {
      if (psessionEntry->protStaCache[i].active)
      {
          PELOG1(limLog(pMac, LOG1, FL("Addr: "));)
          PELOG1(limPrintMacAddr(pMac, psessionEntry->protStaCache[i].addr, LOG1);)

          if (vos_mem_compare(
              psessionEntry->protStaCache[i].addr,
              peerMacAddr, sizeof(tSirMacAddr)))
          {
              PELOG1(limLog(pMac, LOG1, FL("matching cache entry at %d already active."), i);)
              return;
          }
      }
  }

  for (i=0; i<LIM_PROT_STA_CACHE_SIZE; i++)
  {
      if (!psessionEntry->protStaCache[i].active)
          break;
  }

  if (i >= LIM_PROT_STA_CACHE_SIZE)
  {
      PELOGE(limLog(pMac, LOGE, FL("No space in ProtStaCache"));)
      return;
  }

  vos_mem_copy( psessionEntry->protStaCache[i].addr,
                peerMacAddr,
                sizeof(tSirMacAddr));

  psessionEntry->protStaCache[i].protStaCacheType = protStaCacheType;
  psessionEntry->protStaCache[i].active = true;
  if(eLIM_PROT_STA_CACHE_TYPE_llB == protStaCacheType)
  {
      psessionEntry->gLim11bParams.numSta++;
      limLog(pMac,LOG1, FL("11B, "));
  }
  else if(eLIM_PROT_STA_CACHE_TYPE_llG == protStaCacheType)
  {
      psessionEntry->gLim11gParams.numSta++;
      limLog(pMac,LOG1, FL("11G, "));
  }
  else   if(eLIM_PROT_STA_CACHE_TYPE_HT20 == protStaCacheType)
  {
      psessionEntry->gLimHt20Params.numSta++;
      limLog(pMac,LOG1, FL("HT20, "));
  }

  if(!gfSupported)
  {
     psessionEntry->gLimNonGfParams.numSta++;
      limLog(pMac,LOG1, FL("NonGf, "));
  }
  if(!lsigTxopSupported)
  {
      psessionEntry->gLimLsigTxopParams.numSta++;
      limLog(pMac,LOG1, FL("!lsigTxopSupported"));
  }
}// ---------------------------------------------------------------------

/** -------------------------------------------------------------
\fn limDecideApProtection
\brief Decides all the protection related staiton coexistence and also sets
\        short preamble and short slot appropriately. This function will be called
\        when AP is ready to send assocRsp tp the station joining right now.
\param      tpAniSirGlobal    pMac
\param      tSirMacAddr peerMacAddr
\return      None
  -------------------------------------------------------------*/
void
limDecideApProtection(tpAniSirGlobal pMac, tSirMacAddr peerMacAddr, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    tANI_U16              tmpAid;
    tpDphHashNode    pStaDs;
    tSirRFBand  rfBand = SIR_BAND_UNKNOWN;
    tANI_U32 phyMode;
    tLimProtStaCacheType protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_INVALID;
    tHalBitVal gfSupported = eHAL_SET, lsigTxopSupported = eHAL_SET;

    pBeaconParams->paramChangeBitmap = 0;
    // check whether to enable protection or not
    pStaDs = dphLookupHashEntry(pMac, peerMacAddr, &tmpAid, &psessionEntry->dph.dphHashTable);
    if(NULL == pStaDs)
    {
      PELOG1(limLog(pMac, LOG1, FL("pStaDs is NULL"));)
      return;
    }
    limGetRfBand(pMac, &rfBand, psessionEntry);
    //if we are in 5 GHZ band
    if(SIR_BAND_5_GHZ == rfBand)
    {
        //We are 11N. we need to protect from 11A and Ht20. we don't need any other protection in 5 GHZ.
        //HT20 case is common between both the bands and handled down as common code.
        if(true == psessionEntry->htCapability)
        {
            //we are 11N and 11A station is joining.
            //protection from 11A required.
            if(false == pStaDs->mlmStaContext.htCapability)
            {
                limEnable11aProtection(pMac, true, false, pBeaconParams,psessionEntry);
                return;
            }
        }
    }
    else if(SIR_BAND_2_4_GHZ== rfBand)
    {
        limGetPhyMode(pMac, &phyMode, psessionEntry);

        //We are 11G. Check if we need protection from 11b Stations.
        if ((phyMode == WNI_CFG_PHY_MODE_11G) &&
              (false == psessionEntry->htCapability))
        {

            if (pStaDs->erpEnabled== eHAL_CLEAR)
            {
                protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_llB;
                // enable protection
                PELOG3(limLog(pMac, LOG3, FL("Enabling protection from 11B"));)
                limEnable11gProtection(pMac, true, false, pBeaconParams,psessionEntry);
            }
        }

        //HT station.
        if (true == psessionEntry->htCapability)
        {
            //check if we need protection from 11b station
            if ((pStaDs->erpEnabled == eHAL_CLEAR) &&
                (!pStaDs->mlmStaContext.htCapability))
            {
                protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_llB;
                // enable protection
                PELOG3(limLog(pMac, LOG3, FL("Enabling protection from 11B"));)
                limEnable11gProtection(pMac, true, false, pBeaconParams, psessionEntry);
            }
            //station being joined is non-11b and non-ht ==> 11g device
            else if(!pStaDs->mlmStaContext.htCapability)
            {
                protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_llG;
                //enable protection
                limEnableHtProtectionFrom11g(pMac, true, false, pBeaconParams, psessionEntry);
            }
            //ERP mode is enabled for the latest station joined
            //latest station joined is HT capable
            //This case is being handled in common code (commn between both the bands) below.
        }
    }

    //we are HT and HT station is joining. This code is common for both the bands.
    if((true == psessionEntry->htCapability) &&
        (true == pStaDs->mlmStaContext.htCapability))
    {
        if(!pStaDs->htGreenfield)
        {
          limEnableHTNonGfProtection(pMac, true, false, pBeaconParams, psessionEntry);
          gfSupported = eHAL_CLEAR;
        }
        //Station joining is HT 20Mhz
        if((eHT_CHANNEL_WIDTH_20MHZ == pStaDs->htSupportedChannelWidthSet)&&
           (eHT_CHANNEL_WIDTH_20MHZ != psessionEntry->htSupportedChannelWidthSet))
        {
            protStaCacheType = eLIM_PROT_STA_CACHE_TYPE_HT20;
            limEnableHT20Protection(pMac, true, false, pBeaconParams, psessionEntry);
        }
        //Station joining does not support LSIG TXOP Protection
        if(!pStaDs->htLsigTXOPProtection)
        {
            limEnableHTLsigTxopProtection(pMac, false, false, pBeaconParams,psessionEntry);
            lsigTxopSupported = eHAL_CLEAR;
        }
    }

    limUpdateProtStaParams(pMac, peerMacAddr, protStaCacheType,
              gfSupported, lsigTxopSupported, psessionEntry);

    return;
}


/** -------------------------------------------------------------
\fn limEnableOverlap11gProtection
\brief wrapper function for setting overlap 11g protection.
\param      tpAniSirGlobal    pMac
\param      tpUpdateBeaconParams pBeaconParams
\param      tpSirMacMgmtHdr         pMh
\return      None
  -------------------------------------------------------------*/
void
limEnableOverlap11gProtection(tpAniSirGlobal pMac,
tpUpdateBeaconParams pBeaconParams, tpSirMacMgmtHdr pMh,tpPESession psessionEntry)
{
    limUpdateOverlapStaParam(pMac, pMh->bssId, &(psessionEntry->gLimOlbcParams));

    if (psessionEntry->gLimOlbcParams.numSta &&
        !psessionEntry->gLimOlbcParams.protectionEnabled)
    {
        // enable protection
        PELOG1(limLog(pMac, LOG1, FL("OLBC happens!!!"));)
        limEnable11gProtection(pMac, true, true, pBeaconParams,psessionEntry);
    }
}


/** -------------------------------------------------------------
\fn limUpdateShortPreamble
\brief Updates short preamble if needed when a new station joins.
\param      tpAniSirGlobal    pMac
\param      tSirMacAddr peerMacAddr
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
void
limUpdateShortPreamble(tpAniSirGlobal pMac, tSirMacAddr peerMacAddr,
    tpUpdateBeaconParams pBeaconParams, tpPESession psessionEntry)
{
    tANI_U16         tmpAid;
    tpDphHashNode    pStaDs;
    tANI_U32         phyMode;
    tANI_U16         i;

    // check whether to enable protection or not
    pStaDs = dphLookupHashEntry(pMac, peerMacAddr, &tmpAid, &psessionEntry->dph.dphHashTable);

    limGetPhyMode(pMac, &phyMode, psessionEntry);

    if (pStaDs != NULL && phyMode == WNI_CFG_PHY_MODE_11G)

    {
        if (pStaDs->shortPreambleEnabled == eHAL_CLEAR)
        {
            PELOG1(limLog(pMac,LOG1,FL("Short Preamble is not enabled in Assoc Req from "));
                    limPrintMacAddr(pMac, peerMacAddr, LOG1);)

                for (i = 0; i < LIM_PROT_STA_CACHE_SIZE; i++) {
                    if (LIM_IS_AP_ROLE(psessionEntry) &&
                        psessionEntry->gLimNoShortParams.staNoShortCache[i].active) {
                        if (vos_mem_compare(
                                    psessionEntry->gLimNoShortParams.staNoShortCache[i].addr,
                                    peerMacAddr, sizeof(tSirMacAddr)))
                            return;
                    } else if (!LIM_IS_AP_ROLE(psessionEntry)) {
                        if (pMac->lim.gLimNoShortParams.staNoShortCache[i].active) {
                            if (vos_mem_compare(
                                             pMac->lim.gLimNoShortParams.staNoShortCache[i].addr,
                                             peerMacAddr, sizeof(tSirMacAddr)))
                            return;
                        }
                    }
                }

            for (i = 0; i < LIM_PROT_STA_CACHE_SIZE; i++) {
                if (LIM_IS_AP_ROLE(psessionEntry) &&
                      !psessionEntry->gLimNoShortParams.staNoShortCache[i].active)
                     break;
                else {
                    if (!pMac->lim.gLimNoShortParams.staNoShortCache[i].active)
                    break;
                }
            }

            if (i >= LIM_PROT_STA_CACHE_SIZE) {
                if (LIM_IS_AP_ROLE(psessionEntry)) {
                    limLog(pMac, LOGE, FL("No space in Short cache (#active %d, #sta %d) for sta "),
                            i, psessionEntry->gLimNoShortParams.numNonShortPreambleSta);
                    limPrintMacAddr(pMac, peerMacAddr, LOGE);
                    return;
                } else {
                    limLog(pMac, LOGE, FL("No space in Short cache (#active %d, #sta %d) for sta "),
                            i, pMac->lim.gLimNoShortParams.numNonShortPreambleSta);
                    limPrintMacAddr(pMac, peerMacAddr, LOGE);
                    return;
                }
            }

            if (LIM_IS_AP_ROLE(psessionEntry)) {
                vos_mem_copy(psessionEntry->gLimNoShortParams.staNoShortCache[i].addr,
                             peerMacAddr, sizeof(tSirMacAddr));
                psessionEntry->gLimNoShortParams.staNoShortCache[i].active = true;
                psessionEntry->gLimNoShortParams.numNonShortPreambleSta++;
            } else {
                vos_mem_copy(pMac->lim.gLimNoShortParams.staNoShortCache[i].addr,
                             peerMacAddr, sizeof(tSirMacAddr));
                pMac->lim.gLimNoShortParams.staNoShortCache[i].active = true;
                pMac->lim.gLimNoShortParams.numNonShortPreambleSta++;
            }


            // enable long preamble
            PELOG1(limLog(pMac, LOG1, FL("Disabling short preamble"));)

            if (limEnableShortPreamble(pMac, false, pBeaconParams, psessionEntry) != eSIR_SUCCESS)
                PELOGE(limLog(pMac, LOGE, FL("Cannot enable long preamble"));)
        }
    }
}

/** -------------------------------------------------------------
\fn limUpdateShortSlotTime
\brief Updates short slot time if needed when a new station joins.
\param      tpAniSirGlobal    pMac
\param      tSirMacAddr peerMacAddr
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/

void
limUpdateShortSlotTime(tpAniSirGlobal pMac, tSirMacAddr peerMacAddr,
    tpUpdateBeaconParams pBeaconParams, tpPESession psessionEntry)
{
    tANI_U16              tmpAid;
    tpDphHashNode    pStaDs;
    tANI_U32 phyMode;
    tANI_U32 val;
    tANI_U16 i;

    // check whether to enable protection or not
    pStaDs = dphLookupHashEntry(pMac, peerMacAddr, &tmpAid, &psessionEntry->dph.dphHashTable);
    limGetPhyMode(pMac, &phyMode, psessionEntry);

    /* Only in case of softap in 11g mode, slot time might change depending on the STA being added. In 11a case, it should
     * be always 1 and in 11b case, it should be always 0
     */
    if (pStaDs != NULL && phyMode == WNI_CFG_PHY_MODE_11G)
    {
        /* Only when the new STA has short slot time disabled, we need to change softap's overall slot time settings
         * else the default for softap is always short slot enabled. When the last long slot STA leaves softAP, we take care of
         * it in limDecideShortSlot
         */
        if (pStaDs->shortSlotTimeEnabled == eHAL_CLEAR)
        {
            PELOG1(limLog(pMac, LOG1, FL("Short Slot Time is not enabled in Assoc Req from "));
            limPrintMacAddr(pMac, peerMacAddr, LOG1);)
            for (i = 0; i < LIM_PROT_STA_CACHE_SIZE; i++)
            {
                if (LIM_IS_AP_ROLE(psessionEntry) &&
                     psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].active) {
                    if (vos_mem_compare(
                         psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].addr,
                         peerMacAddr, sizeof(tSirMacAddr)))
                        return;
                } else if (!LIM_IS_AP_ROLE(psessionEntry)) {
                    if (pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].active)
                    {
                        if (vos_mem_compare(
                            pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].addr,
                            peerMacAddr, sizeof(tSirMacAddr)))
                            return;
                     }
                }
            }

            for (i = 0; i < LIM_PROT_STA_CACHE_SIZE; i++) {
                if (LIM_IS_AP_ROLE(psessionEntry) &&
                   !psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].active)
                    break;
                else {
                    if (!pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].active)
                        break;
                 }
            }

            if (i >= LIM_PROT_STA_CACHE_SIZE) {
                if (LIM_IS_AP_ROLE(psessionEntry)) {
                    limLog(pMac, LOGE, FL("No space in ShortSlot cache (#active %d, #sta %d) for sta "),
                            i, psessionEntry->gLimNoShortSlotParams.numNonShortSlotSta);
                    limPrintMacAddr(pMac, peerMacAddr, LOGE);
                    return;
                } else {
                    limLog(pMac, LOGE, FL("No space in ShortSlot cache (#active %d, #sta %d) for sta "),
                           i, pMac->lim.gLimNoShortSlotParams.numNonShortSlotSta);
                    limPrintMacAddr(pMac, peerMacAddr, LOGE);
                    return;
                }
            }

            if (LIM_IS_AP_ROLE(psessionEntry)) {
                vos_mem_copy(psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].addr,
                             peerMacAddr, sizeof(tSirMacAddr));
                psessionEntry->gLimNoShortSlotParams.staNoShortSlotCache[i].active = true;
                psessionEntry->gLimNoShortSlotParams.numNonShortSlotSta++;
            } else {
                vos_mem_copy( pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].addr,
                          peerMacAddr, sizeof(tSirMacAddr));
                pMac->lim.gLimNoShortSlotParams.staNoShortSlotCache[i].active = true;
                pMac->lim.gLimNoShortSlotParams.numNonShortSlotSta++;
            }
            wlan_cfgGetInt(pMac, WNI_CFG_11G_SHORT_SLOT_TIME_ENABLED, &val);

            /* Here we check if we are AP role and short slot enabled (both admin and oper modes) but we have atleast one STA connected with
             * only long slot enabled, we need to change our beacon/pb rsp to broadcast short slot disabled
             */
            if ((LIM_IS_AP_ROLE(psessionEntry)) && (val &&
                 psessionEntry->gLimNoShortSlotParams.numNonShortSlotSta &&
                 psessionEntry->shortSlotTimeSupported)) {
                // enable long slot time
                pBeaconParams->fShortSlotTime = false;
                pBeaconParams->paramChangeBitmap |= PARAM_SHORT_SLOT_TIME_CHANGED;
                PELOG1(limLog(pMac, LOG1, FL("Disable short slot time. Enable long slot time."));)
                psessionEntry->shortSlotTimeSupported = false;
            } else if (!LIM_IS_AP_ROLE(psessionEntry)) {
                if (val && pMac->lim.gLimNoShortSlotParams.numNonShortSlotSta &&
                    psessionEntry->shortSlotTimeSupported) {
                    // enable long slot time
                    pBeaconParams->fShortSlotTime = false;
                    pBeaconParams->paramChangeBitmap |= PARAM_SHORT_SLOT_TIME_CHANGED;
                    PELOG1(limLog(pMac, LOG1, FL("Disable short slot time. Enable long slot time."));)
                    psessionEntry->shortSlotTimeSupported = false;
                }
            }
        }
    }
}


/** -------------------------------------------------------------
\fn limDecideStaProtectionOnAssoc
\brief Decide protection related settings on Sta while association.
\param      tpAniSirGlobal    pMac
\param      tpSchBeaconStruct pBeaconStruct
\return      None
  -------------------------------------------------------------*/
void
limDecideStaProtectionOnAssoc(tpAniSirGlobal pMac,
    tpSchBeaconStruct pBeaconStruct, tpPESession psessionEntry)
{
    tSirRFBand rfBand = SIR_BAND_UNKNOWN;
    tANI_U32 phyMode = WNI_CFG_PHY_MODE_NONE;

    limGetRfBand(pMac, &rfBand, psessionEntry);
    limGetPhyMode(pMac, &phyMode, psessionEntry);

    if(SIR_BAND_5_GHZ == rfBand)
    {
        if((eSIR_HT_OP_MODE_MIXED == pBeaconStruct->HTInfo.opMode)  ||
                    (eSIR_HT_OP_MODE_OVERLAP_LEGACY == pBeaconStruct->HTInfo.opMode))
        {
            if(pMac->lim.cfgProtection.fromlla)
                psessionEntry->beaconParams.llaCoexist = true;
        }
        else if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == pBeaconStruct->HTInfo.opMode)
        {
            if(pMac->lim.cfgProtection.ht20)
                psessionEntry->beaconParams.ht20Coexist = true;
        }

    }
    else if(SIR_BAND_2_4_GHZ == rfBand)
    {
        //spec 7.3.2.13
        //UseProtection will be set when nonERP STA is associated.
        //NonERPPresent bit will be set when:
        //--nonERP Sta is associated OR
        //--nonERP Sta exists in overlapping BSS
        //when useProtection is not set then protection from nonERP stations is optional.

        //CFG protection from 11b is enabled and
        //11B device in the BSS
         /* TODO, This is not sessionized */
        if (phyMode != WNI_CFG_PHY_MODE_11B)
        {
            if (pMac->lim.cfgProtection.fromllb &&
                pBeaconStruct->erpPresent &&
                (pBeaconStruct->erpIEInfo.useProtection ||
                pBeaconStruct->erpIEInfo.nonErpPresent))
            {
                psessionEntry->beaconParams.llbCoexist = true;
            }
            //AP has no 11b station associated.
            else
            {
                psessionEntry->beaconParams.llbCoexist = false;
            }
        }
        //following code block is only for HT station.
        if((psessionEntry->htCapability) &&
              (pBeaconStruct->HTInfo.present))
        {
            tDot11fIEHTInfo htInfo = pBeaconStruct->HTInfo;

            //Obss Non HT STA present mode
            psessionEntry->beaconParams.gHTObssMode =  (tANI_U8)htInfo.obssNonHTStaPresent;


          //CFG protection from 11G is enabled and
            //our AP has at least one 11G station associated.
            if(pMac->lim.cfgProtection.fromllg &&
                  ((eSIR_HT_OP_MODE_MIXED == htInfo.opMode)  ||
                        (eSIR_HT_OP_MODE_OVERLAP_LEGACY == htInfo.opMode))&&
                      (!psessionEntry->beaconParams.llbCoexist))
            {
                if(pMac->lim.cfgProtection.fromllg)
                    psessionEntry->beaconParams.llgCoexist = true;
            }

            //AP has only HT stations associated and at least one station is HT 20
            //disable protection from any non-HT devices.
            //decision for disabling protection from 11b has already been taken above.
            if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == htInfo.opMode)
            {
                //Disable protection from 11G station.
                psessionEntry->beaconParams.llgCoexist = false;
          //CFG protection from HT 20 is enabled.
          if(pMac->lim.cfgProtection.ht20)
                psessionEntry->beaconParams.ht20Coexist = true;
            }
            //Disable protection from non-HT and HT20 devices.
            //decision for disabling protection from 11b has already been taken above.
            if(eSIR_HT_OP_MODE_PURE == htInfo.opMode)
            {
                psessionEntry->beaconParams.llgCoexist = false;
                psessionEntry->beaconParams.ht20Coexist = false;
            }

        }
    }

    //protection related factors other than HT operating mode. Applies to 2.4 GHZ as well as 5 GHZ.
    if((psessionEntry->htCapability) &&
          (pBeaconStruct->HTInfo.present))
    {
        tDot11fIEHTInfo htInfo = pBeaconStruct->HTInfo;
        psessionEntry->beaconParams.fRIFSMode =
            ( tANI_U8 ) htInfo.rifsMode;
        psessionEntry->beaconParams.llnNonGFCoexist =
            ( tANI_U8 )htInfo.nonGFDevicesPresent;
        psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport =
            ( tANI_U8 )htInfo.lsigTXOPProtectionFullSupport;
    }
}


/** -------------------------------------------------------------
\fn limDecideStaProtection
\brief Decides protection related settings on Sta while processing beacon.
\param      tpAniSirGlobal    pMac
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
void
limDecideStaProtection(tpAniSirGlobal pMac,
    tpSchBeaconStruct pBeaconStruct, tpUpdateBeaconParams pBeaconParams, tpPESession psessionEntry)
{

    tSirRFBand rfBand = SIR_BAND_UNKNOWN;
    tANI_U32 phyMode = WNI_CFG_PHY_MODE_NONE;

    limGetRfBand(pMac, &rfBand, psessionEntry);
    limGetPhyMode(pMac, &phyMode, psessionEntry);

    if(SIR_BAND_5_GHZ == rfBand)
    {
        //we are HT capable.
        if((true == psessionEntry->htCapability) &&
            (pBeaconStruct->HTInfo.present))
        {
            //we are HT capable, AP's HT OPMode is mixed / overlap legacy ==> need protection from 11A.
            if((eSIR_HT_OP_MODE_MIXED == pBeaconStruct->HTInfo.opMode) ||
              (eSIR_HT_OP_MODE_OVERLAP_LEGACY == pBeaconStruct->HTInfo.opMode))
            {
                limEnable11aProtection(pMac, true, false, pBeaconParams,psessionEntry);
            }
            //we are HT capable, AP's HT OPMode is HT20 ==> disable protection from 11A if enabled. enabled
            //protection from HT20 if needed.
            else if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT== pBeaconStruct->HTInfo.opMode)
            {
                limEnable11aProtection(pMac, false, false, pBeaconParams,psessionEntry);
                limEnableHT20Protection(pMac, true, false, pBeaconParams,psessionEntry);
            }
            else if(eSIR_HT_OP_MODE_PURE == pBeaconStruct->HTInfo.opMode)
            {
                limEnable11aProtection(pMac, false, false, pBeaconParams,psessionEntry);
                limEnableHT20Protection(pMac, false, false, pBeaconParams,psessionEntry);
            }
        }
    }
    else if(SIR_BAND_2_4_GHZ == rfBand)
    {
        /* spec 7.3.2.13
         * UseProtection will be set when nonERP STA is associated.
         * NonERPPresent bit will be set when:
         * --nonERP Sta is associated OR
         * --nonERP Sta exists in overlapping BSS
         * when useProtection is not set then protection from nonERP stations is optional.
         */

        if (phyMode != WNI_CFG_PHY_MODE_11B)
        {
            if (pBeaconStruct->erpPresent &&
                  (pBeaconStruct->erpIEInfo.useProtection ||
                  pBeaconStruct->erpIEInfo.nonErpPresent))
            {
                limEnable11gProtection(pMac, true, false, pBeaconParams, psessionEntry);
            }
            //AP has no 11b station associated.
            else
            {
                //disable protection from 11b station
                limEnable11gProtection(pMac, false, false, pBeaconParams, psessionEntry);
            }
         }

        //following code block is only for HT station.
        if((psessionEntry->htCapability) &&
              (pBeaconStruct->HTInfo.present))
        {

            tDot11fIEHTInfo htInfo = pBeaconStruct->HTInfo;
            //AP has at least one 11G station associated.
            if(((eSIR_HT_OP_MODE_MIXED == htInfo.opMode)  ||
                  (eSIR_HT_OP_MODE_OVERLAP_LEGACY == htInfo.opMode))&&
                (!psessionEntry->beaconParams.llbCoexist))
            {
                limEnableHtProtectionFrom11g(pMac, true, false, pBeaconParams,psessionEntry);

            }

            //no HT operating mode change  ==> no change in protection settings except for MIXED_MODE/Legacy Mode.
            //in Mixed mode/legacy Mode even if there is no change in HT operating mode, there might be change in 11bCoexist
            //or 11gCoexist. that is why this check is being done after mixed/legacy mode check.
            if ( pMac->lim.gHTOperMode != ( tSirMacHTOperatingMode )htInfo.opMode )
            {
                pMac->lim.gHTOperMode       = ( tSirMacHTOperatingMode )htInfo.opMode;

                 //AP has only HT stations associated and at least one station is HT 20
                 //disable protection from any non-HT devices.
                 //decision for disabling protection from 11b has already been taken above.
                if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == htInfo.opMode)
                {
                    //Disable protection from 11G station.
                    limEnableHtProtectionFrom11g(pMac, false, false, pBeaconParams,psessionEntry);

                    limEnableHT20Protection(pMac, true, false, pBeaconParams,psessionEntry);
                }
                //Disable protection from non-HT and HT20 devices.
                //decision for disabling protection from 11b has already been taken above.
                else if(eSIR_HT_OP_MODE_PURE == htInfo.opMode)
                {
                    limEnableHtProtectionFrom11g(pMac, false, false, pBeaconParams,psessionEntry);
                    limEnableHT20Protection(pMac, false, false, pBeaconParams,psessionEntry);

                }
            }
        }
    }

    //following code block is only for HT station. ( 2.4 GHZ as well as 5 GHZ)
    if((psessionEntry->htCapability) &&
          (pBeaconStruct->HTInfo.present))
    {
        tDot11fIEHTInfo htInfo = pBeaconStruct->HTInfo;
        //Check for changes in protection related factors other than HT operating mode.
        //Check for changes in RIFS mode, nonGFDevicesPresent, lsigTXOPProtectionFullSupport.
        if ( psessionEntry->beaconParams.fRIFSMode !=
                ( tANI_U8 ) htInfo.rifsMode )
        {
            pBeaconParams->fRIFSMode =
                psessionEntry->beaconParams.fRIFSMode  =
                ( tANI_U8 ) htInfo.rifsMode;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }

        if ( psessionEntry->beaconParams.llnNonGFCoexist !=
                htInfo.nonGFDevicesPresent )
        {
            pBeaconParams->llnNonGFCoexist =
                psessionEntry->beaconParams.llnNonGFCoexist =
                ( tANI_U8 )htInfo.nonGFDevicesPresent;
            pBeaconParams->paramChangeBitmap |=
                PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }

        if ( psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport !=
                ( tANI_U8 )htInfo.lsigTXOPProtectionFullSupport )
        {
            pBeaconParams->fLsigTXOPProtectionFullSupport =
                psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport =
                ( tANI_U8 )htInfo.lsigTXOPProtectionFullSupport;
            pBeaconParams->paramChangeBitmap |=
                PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }

    // For Station just update the global lim variable, no need to send message to HAL
    // Station already taking care of HT OPR Mode=01, meaning AP is seeing legacy
    //stations in overlapping BSS.
       if ( psessionEntry->beaconParams.gHTObssMode != ( tANI_U8 )htInfo.obssNonHTStaPresent )
            psessionEntry->beaconParams.gHTObssMode = ( tANI_U8 )htInfo.obssNonHTStaPresent ;

    }
}


/**
 * limProcessChannelSwitchTimeout()
 *
 *FUNCTION:
 * This function is invoked when Channel Switch Timer expires at
 * the STA.  Now, STA must stop traffic, and then change/disable
 * primary or secondary channel.
 *
 *
 *NOTE:
 * @param  pMac           - Pointer to Global MAC structure
 * @return None
 */
void limProcessChannelSwitchTimeout(tpAniSirGlobal pMac)
{
    tpPESession psessionEntry = NULL;
    tANI_U8    channel; // This is received and stored from channelSwitch Action frame
    tANI_U8 isSessionPowerActive = false;

    psessionEntry = peFindSessionBySessionId(pMac,
                       pMac->lim.limTimers.gLimChannelSwitchTimer.sessionId);
    if (!psessionEntry) {
        limLog(pMac, LOGW, FL("Session Does not exist for given sessionID"));
        return;
    }

    if (!LIM_IS_STA_ROLE(psessionEntry)) {
        PELOGW(limLog(pMac, LOGW,
               "Channel switch can be done only in STA role, Current Role = %d",
               GET_LIM_SYSTEM_ROLE(psessionEntry));)
        return;
    }
    if (psessionEntry->gLimSpecMgmt.dot11hChanSwState !=
                                  eLIM_11H_CHANSW_RUNNING) {
        limLog(pMac, LOGW,
            FL("Channel switch timer should not have been running in state %d"),
            psessionEntry->gLimSpecMgmt.dot11hChanSwState);
        return;
    }

    if(pMac->psOffloadEnabled)
    {
        isSessionPowerActive = pmmPsOffloadIsActive(pMac, psessionEntry);
    }
    else
    {
        isSessionPowerActive = limIsSystemInActiveState(pMac);
    }
    channel = psessionEntry->gLimChannelSwitch.primaryChannel;

    /*
     *  This potentially can create issues if the function tries to set
     * channel while device is in power-save, hence putting an extra check
     * to verify if the device is in power-save or not
     */
    if(!isSessionPowerActive)
    {
        PELOGW(limLog(pMac, LOGW, FL("Device is not in active state, cannot switch channel"));)
        return;
    }

    // Restore Channel Switch parameters to default
    psessionEntry->gLimChannelSwitch.switchTimeoutValue = 0;

    /* Channel-switch timeout has occurred. reset the state */
    psessionEntry->gLimSpecMgmt.dot11hChanSwState = eLIM_11H_CHANSW_END;

    /*
     * If Lim allows Switch channel on same channel on which preauth
     * is going on then LIM will not post resume link(WDA_FINISH_SCAN)
     * during preauth rsp handling hence firmware may crash on ENTER/
     * EXIT BMPS request.
     */
    if(psessionEntry->ftPEContext.pFTPreAuthReq)
    {
        limLog(pMac, LOGE,
           FL("Avoid Switch Channel req during pre auth"));
        return;
    }
    /* If link is already suspended mean some off
     * channel operation or scan is in progress, Allowing
     * Change channel here will lead to either Init Scan
     * sent twice or missing Finish scan when change
     * channel is completed, this may lead
     * to driver in invalid state and crash.
     */
    if (limIsLinkSuspended(pMac))
    {
       limLog(pMac, LOGE, FL("Link is already suspended for "
               "some other reason. Return here for sessionId:%d"),
               pMac->lim.limTimers.gLimChannelSwitchTimer.sessionId);
       return;
    }

    /* Check if the AP is switching to a channel that we support.
     * Else, just don't bother to switch. Indicate HDD to look for a
     * better AP to associate
     */
    if(!limIsChannelValidForChannelSwitch(pMac, channel))
    {
        /* We need to restore pre-channelSwitch state on the STA */
        if(limRestorePreChannelSwitchState(pMac, psessionEntry) != eSIR_SUCCESS)
        {
            limLog(pMac, LOGP, FL("Could not restore pre-channelSwitch (11h) state, resetting the system"));
            return;
        }

        /*
         * The channel switch request received from AP is carrying
		 * invalid channel. It's ok to ignore this channel switch
		 * request as it might be from spoof AP. If it's from genuine
		 * AP, it may lead to heart beat failure and result in
		 * disconnection. DUT can go ahead and reconnect to it/any
		 * other AP once it disconnects.
         */
        limLog(pMac, LOGE, FL("Invalid channel freq %u Ignore CSA request"),
               channel);
        return;
    }
    limCovertChannelScanType(pMac, psessionEntry->currentOperChannel, false);
    pMac->lim.dfschannelList.timeStamp[psessionEntry->currentOperChannel] = 0;
    switch(psessionEntry->gLimChannelSwitch.state)
    {
        case eLIM_CHANNEL_SWITCH_PRIMARY_ONLY:
            PELOGW(limLog(pMac, LOGW, FL("CHANNEL_SWITCH_PRIMARY_ONLY "));)
            limSwitchPrimaryChannel(pMac, psessionEntry->gLimChannelSwitch.primaryChannel,psessionEntry);
            psessionEntry->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_IDLE;
            break;

        case eLIM_CHANNEL_SWITCH_SECONDARY_ONLY:
            PELOGW(limLog(pMac, LOGW, FL("CHANNEL_SWITCH_SECONDARY_ONLY "));)
            limSwitchPrimarySecondaryChannel(pMac, psessionEntry,
                                             psessionEntry->currentOperChannel,
                                             psessionEntry->gLimChannelSwitch.secondarySubBand);
            psessionEntry->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_IDLE;
            break;

        case eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY:
            PELOGW(limLog(pMac, LOGW, FL("CHANNEL_SWITCH_PRIMARY_AND_SECONDARY"));)
            limSwitchPrimarySecondaryChannel(pMac, psessionEntry,
                                             psessionEntry->gLimChannelSwitch.primaryChannel,
                                             psessionEntry->gLimChannelSwitch.secondarySubBand);
            psessionEntry->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_IDLE;
            break;

        case eLIM_CHANNEL_SWITCH_IDLE:
        default:
            PELOGE(limLog(pMac, LOGE, FL("incorrect state "));)
            if(limRestorePreChannelSwitchState(pMac, psessionEntry) != eSIR_SUCCESS)
            {
                limLog(pMac, LOGP, FL("Could not restore pre-channelSwitch (11h) state, resetting the system"));
            }
            return;  /* Please note, this is 'return' and not 'break' */
    }
}

/**
 * limUpdateChannelSwitch()
 *
 *FUNCTION:
 * This function is invoked whenever Station receives
 * either 802.11h channel switch IE or airgo proprietary
 * channel switch IE.
 *
 *NOTE:
 * @param  pMac           - Pointer to Global MAC structure
 * @return  tpSirProbeRespBeacon - Pointer to Beacon/Probe Rsp
 * @param psessionentry
 */
void
limUpdateChannelSwitch(struct sAniSirGlobal *pMac, tpSirProbeRespBeacon pBeacon,
                       tpPESession psessionEntry)
{
    tANI_U16                         beaconPeriod;
    tDot11fIEChanSwitchAnn           *pChnlSwitch;
#ifdef WLAN_FEATURE_11AC
    tDot11fIEWiderBWChanSwitchAnn    *pWiderChnlSwitch;
#endif

    beaconPeriod = psessionEntry->beaconParams.beaconInterval;

    /* 802.11h standard channel switch IE */
    pChnlSwitch = &(pBeacon->channelSwitchIE);
    psessionEntry->gLimChannelSwitch.primaryChannel = pChnlSwitch->newChannel;
    psessionEntry->gLimChannelSwitch.switchCount = pChnlSwitch->switchCount;
    psessionEntry->gLimChannelSwitch.switchTimeoutValue =
              SYS_MS_TO_TICKS(beaconPeriod)* (pChnlSwitch->switchCount);
    psessionEntry->gLimChannelSwitch.switchMode = pChnlSwitch->switchMode;
#ifdef WLAN_FEATURE_11AC
    pWiderChnlSwitch = &(pBeacon->WiderBWChanSwitchAnn);
    if (pBeacon->WiderBWChanSwitchAnnPresent) {
        psessionEntry->gLimWiderBWChannelSwitch.newChanWidth =
                                                 pWiderChnlSwitch->newChanWidth;
        psessionEntry->gLimWiderBWChannelSwitch.newCenterChanFreq0 =
                                           pWiderChnlSwitch->newCenterChanFreq0;
        psessionEntry->gLimWiderBWChannelSwitch.newCenterChanFreq1 =
                                           pWiderChnlSwitch->newCenterChanFreq1;
    }
#endif

     /* Only primary channel switch element is present */
     psessionEntry->gLimChannelSwitch.state = eLIM_CHANNEL_SWITCH_PRIMARY_ONLY;
     psessionEntry->gLimChannelSwitch.secondarySubBand =
                                                   PHY_SINGLE_CHANNEL_CENTERED;

     /* Do not bother to look and operate on extended channel switch element
      * if our own channel-bonding state is not enabled
      */
     if (psessionEntry->htSupportedChannelWidthSet) {
         if (pBeacon->sec_chan_offset_present) {
             if ((pBeacon->sec_chan_offset.secondaryChannelOffset ==
                                 PHY_DOUBLE_CHANNEL_LOW_PRIMARY) ||
                 (pBeacon->sec_chan_offset.secondaryChannelOffset ==
                                 PHY_DOUBLE_CHANNEL_HIGH_PRIMARY)) {
                 psessionEntry->gLimChannelSwitch.state =
                                      eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY;
                 psessionEntry->gLimChannelSwitch.secondarySubBand =
                             pBeacon->sec_chan_offset.secondaryChannelOffset;
             }
#ifdef WLAN_FEATURE_11AC
             if (psessionEntry->vhtCapability &&
                               pBeacon->WiderBWChanSwitchAnnPresent) {
                 if (pWiderChnlSwitch->newChanWidth ==
                                   WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ) {
                     if (pBeacon->sec_chan_offset_present) {
                         if ((pBeacon->sec_chan_offset.secondaryChannelOffset ==
                                              PHY_DOUBLE_CHANNEL_LOW_PRIMARY) ||
                             (pBeacon->sec_chan_offset.secondaryChannelOffset ==
                                              PHY_DOUBLE_CHANNEL_HIGH_PRIMARY)) {
                             psessionEntry->gLimChannelSwitch.state =
                                      eLIM_CHANNEL_SWITCH_PRIMARY_AND_SECONDARY;
                             psessionEntry->gLimChannelSwitch.secondarySubBand =
                                limGet11ACPhyCBState(pMac,
                                    psessionEntry->gLimChannelSwitch.primaryChannel,
                                    pBeacon->sec_chan_offset.secondaryChannelOffset,
                                    pWiderChnlSwitch->newCenterChanFreq0,
                                    psessionEntry);
                         }
                     }
                 }
             }
#endif
         }
    }
    if (eSIR_SUCCESS != limStartChannelSwitch(pMac, psessionEntry)) {
        PELOGW(limLog(pMac, LOGW, FL("Could not start Channel Switch"));)
    }

    limLog(pMac, LOGW,
        FL("session %d primary chl %d, subband %d, count  %d (%d ticks)"),
        psessionEntry->peSessionId,
        psessionEntry->gLimChannelSwitch.primaryChannel,
        psessionEntry->gLimChannelSwitch.secondarySubBand,
        psessionEntry->gLimChannelSwitch.switchCount,
        psessionEntry->gLimChannelSwitch.switchTimeoutValue);
    return;
}

/**
 * limCancelDot11hChannelSwitch
 *
 *FUNCTION:
 * This function is called when STA does not send updated channel-swith IE
 * after indicating channel-switch start. This will cancel the channel-swith
 * timer which is already running.
 *
 *LOGIC:
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac    - Pointer to Global MAC structure
 *
 * @return None
 */
void limCancelDot11hChannelSwitch(tpAniSirGlobal pMac, tpPESession psessionEntry)
{
    if (!LIM_IS_STA_ROLE(psessionEntry))
        return;

    PELOGW(limLog(pMac, LOGW, FL("Received a beacon without channel switch IE"));)
    MTRACE(macTrace(pMac, TRACE_CODE_TIMER_DEACTIVATE, psessionEntry->peSessionId, eLIM_CHANNEL_SWITCH_TIMER));

    if (tx_timer_deactivate(&pMac->lim.limTimers.gLimChannelSwitchTimer) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("tx_timer_deactivate failed!"));)
    }

    /* We need to restore pre-channelSwitch state on the STA */
    if (limRestorePreChannelSwitchState(pMac, psessionEntry) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("LIM: Could not restore pre-channelSwitch (11h) state, resetting the system"));)

    }
}

/**----------------------------------------------
\fn     limCancelDot11hQuiet
\brief  Cancel the quieting on Station if latest
        beacon doesn't contain quiet IE in it.

\param  pMac
\return NONE
-----------------------------------------------*/
void limCancelDot11hQuiet(tpAniSirGlobal pMac, tpPESession psessionEntry)
{
    if (!LIM_IS_STA_ROLE(psessionEntry))
        return;

    if (psessionEntry->gLimSpecMgmt.quietState == eLIM_QUIET_BEGIN)
    {
         MTRACE(macTrace(pMac, TRACE_CODE_TIMER_DEACTIVATE, psessionEntry->peSessionId, eLIM_QUIET_TIMER));
        if (tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietTimer) != TX_SUCCESS)
        {
            PELOGE(limLog(pMac, LOGE, FL("tx_timer_deactivate failed"));)
        }
    }
    else if (psessionEntry->gLimSpecMgmt.quietState == eLIM_QUIET_RUNNING)
    {
        MTRACE(macTrace(pMac, TRACE_CODE_TIMER_DEACTIVATE, psessionEntry->peSessionId, eLIM_QUIET_BSS_TIMER));
        if (tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietBssTimer) != TX_SUCCESS)
        {
            PELOGE(limLog(pMac, LOGE, FL("tx_timer_deactivate failed"));)
        }
        /**
         * If the channel switch is already running in silent mode, dont resume the
         * transmission. Channel switch timer when timeout, transmission will be resumed.
         */
        if(!((psessionEntry->gLimSpecMgmt.dot11hChanSwState == eLIM_11H_CHANSW_RUNNING) &&
                (psessionEntry->gLimChannelSwitch.switchMode == eSIR_CHANSW_MODE_SILENT)))
        {
            limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_RESUME_TX);
            limRestorePreQuietState(pMac, psessionEntry);
        }
    }
    psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_INIT;
}

/**
 * limProcessQuietTimeout
 *
 * FUNCTION:
 * This function is active only on the STA.
 * Handles SIR_LIM_QUIET_TIMEOUT
 *
 * LOGIC:
 * This timeout can occur under only one circumstance:
 *
 * 1) When gLimQuietState = eLIM_QUIET_BEGIN
 * This indicates that the timeout "interval" has
 * expired. This is a trigger for the STA to now
 * shut-off Tx/Rx for the specified gLimQuietDuration
 * -> The TIMER object gLimQuietBssTimer is
 * activated
 * -> With timeout = gLimQuietDuration
 * -> gLimQuietState is set to eLIM_QUIET_RUNNING
 *
 * ASSUMPTIONS:
 * Using two TIMER objects -
 * gLimQuietTimer & gLimQuietBssTimer
 *
 * NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 *
 * @return None
 */
void limProcessQuietTimeout(tpAniSirGlobal pMac)
{
    tpPESession psessionEntry;

    if((psessionEntry = peFindSessionBySessionId(pMac, pMac->lim.limTimers.gLimQuietTimer.sessionId))== NULL)
    {
        limLog(pMac, LOGE,FL("Session Does not exist for given sessionID"));
        return;
    }

  PELOG1(limLog(pMac, LOG1, FL("quietState = %d"), psessionEntry->gLimSpecMgmt.quietState);)
  switch( psessionEntry->gLimSpecMgmt.quietState )
  {
    case eLIM_QUIET_BEGIN:
      // Time to Stop data traffic for quietDuration
      //limDeactivateAndChangeTimer(pMac, eLIM_QUIET_BSS_TIMER);
      if (TX_SUCCESS !=
      tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietBssTimer))
      {
          limLog( pMac, LOGE,
            FL("Unable to de-activate gLimQuietBssTimer! Will attempt to activate anyway..."));
      }

      // gLimQuietDuration appears to be in units of ticks
      // Use it as is
      if (TX_SUCCESS !=
          tx_timer_change( &pMac->lim.limTimers.gLimQuietBssTimer,
            psessionEntry->gLimSpecMgmt.quietDuration,
            0))
      {
          limLog( pMac, LOGE,
            FL("Unable to change gLimQuietBssTimer! Will still attempt to activate anyway..."));
      }
      MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE, pMac->lim.limTimers.gLimQuietTimer.sessionId, eLIM_QUIET_BSS_TIMER));
      if( TX_SUCCESS !=
          tx_timer_activate( &pMac->lim.limTimers.gLimQuietBssTimer ))
      {
        limLog( pMac, LOGW,
            FL("Unable to activate gLimQuietBssTimer! The STA will be unable to honor Quiet BSS..."));
      }
      else
      {
        // Transition to eLIM_QUIET_RUNNING
        psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_RUNNING;

        /* If we have sta bk scan triggered and trigger bk scan actually started successfully, */
        /* print message, otherwise, stop data traffic and stay quiet */
        if( pMac->lim.gLimTriggerBackgroundScanDuringQuietBss &&
          (eSIR_TRUE == (glimTriggerBackgroundScanDuringQuietBss_Status = limTriggerBackgroundScanDuringQuietBss( pMac ))) )
        {
           limLog( pMac, LOG2,
               FL("Attempting to trigger a background scan..."));
        }
        else
        {
           // Shut-off Tx/Rx for gLimSpecMgmt.quietDuration
           /* freeze the transmission */
           limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_STOP_TX);

           limLog( pMac, LOG2,
                FL("Quiet BSS: STA shutting down for %d ticks"),
                psessionEntry->gLimSpecMgmt.quietDuration );
        }
      }
      break;

    case eLIM_QUIET_RUNNING:
    case eLIM_QUIET_INIT:
    case eLIM_QUIET_END:
    default:
      //
      // As of now, nothing to be done
      //
      break;
  }
}

/**
 * limProcessQuietBssTimeout
 *
 * FUNCTION:
 * This function is active on the AP and STA.
 * Handles SIR_LIM_QUIET_BSS_TIMEOUT
 *
 * LOGIC:
 * On the AP -
 * When the SIR_LIM_QUIET_BSS_TIMEOUT is triggered, it is
 * an indication for the AP to START sending out the
 * Quiet BSS IE.
 * If 802.11H is enabled, the Quiet BSS IE is sent as per
 * the 11H spec
 * If 802.11H is not enabled, the Quiet BSS IE is sent as
 * a Proprietary IE.
 * Transitioning gLimQuietState to eLIM_QUIET_BEGIN will
 * initiate the SCH to include the Quiet BSS IE in all
 * its subsequent Beacons/PR's.
 * The Quiet BSS IE will be included in all the Beacons
 * & PR's until the next DTIM period
 *
 * On the STA -
 * When gLimQuietState = eLIM_QUIET_RUNNING
 * This indicates that the STA was successfully shut-off
 * for the specified gLimQuietDuration. This is a trigger
 * for the STA to now resume data traffic.
 * -> gLimQuietState is set to eLIM_QUIET_INIT
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param  pMac - Pointer to Global MAC structure
 *
 * @return None
 */
void limProcessQuietBssTimeout( tpAniSirGlobal pMac )
{
    tpPESession psessionEntry;

    if((psessionEntry = peFindSessionBySessionId(pMac, pMac->lim.limTimers.gLimQuietBssTimer.sessionId))== NULL)
    {
        limLog(pMac, LOGP,FL("Session Does not exist for given sessionID"));
        return;
    }

  PELOG1(limLog(pMac, LOG1, FL("quietState = %d"), psessionEntry->gLimSpecMgmt.quietState);)
  if (LIM_IS_AP_ROLE(psessionEntry)) {
  } else {
    // eLIM_STA_ROLE
    switch( psessionEntry->gLimSpecMgmt.quietState )
    {
      case eLIM_QUIET_RUNNING:
        // Transition to eLIM_QUIET_INIT
        psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_INIT;

        if( !pMac->lim.gLimTriggerBackgroundScanDuringQuietBss || (glimTriggerBackgroundScanDuringQuietBss_Status == eSIR_FALSE) )
        {
          // Resume data traffic only if channel switch is not running in silent mode.
          if (!((psessionEntry->gLimSpecMgmt.dot11hChanSwState == eLIM_11H_CHANSW_RUNNING) &&
                  (psessionEntry->gLimChannelSwitch.switchMode == eSIR_CHANSW_MODE_SILENT)))
          {
              limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_RESUME_TX);
              limRestorePreQuietState(pMac, psessionEntry);
          }

          /* Reset status flag */
          if(glimTriggerBackgroundScanDuringQuietBss_Status == eSIR_FALSE)
              glimTriggerBackgroundScanDuringQuietBss_Status = eSIR_TRUE;

          limLog( pMac, LOG2,
              FL("Quiet BSS: Resuming traffic..."));
        }
        else
        {
          //
          // Nothing specific to be done in this case
          // A background scan that was triggered during
          // SIR_LIM_QUIET_TIMEOUT will complete on its own
          //
          limLog( pMac, LOG2,
              FL("Background scan should be complete now..."));
        }
        break;

      case eLIM_QUIET_INIT:
      case eLIM_QUIET_BEGIN:
      case eLIM_QUIET_END:
        PELOG2(limLog(pMac, LOG2, FL("Quiet state not in RUNNING"));)
        /* If the quiet period has ended, then resume the frame transmission */
        limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_RESUME_TX);
        limRestorePreQuietState(pMac, psessionEntry);
        psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_INIT;
        break;

      default:
        //
        // As of now, nothing to be done
        //
        break;
    }
  }
}

/**----------------------------------------------
\fn        limStartQuietTimer
\brief    Starts the quiet timer.

\param pMac
\return NONE
-----------------------------------------------*/
void limStartQuietTimer(tpAniSirGlobal pMac, tANI_U8 sessionId)
{
    tpPESession psessionEntry;
    psessionEntry = peFindSessionBySessionId(pMac, sessionId);

    if(psessionEntry == NULL) {
        limLog(pMac, LOGP,FL("Session Does not exist for given sessionID"));
        return;
    }

    if (!LIM_IS_STA_ROLE(psessionEntry))
        return;

    // First, de-activate Timer, if its already active
    limCancelDot11hQuiet(pMac, psessionEntry);

    MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE, sessionId, eLIM_QUIET_TIMER));
    if( TX_SUCCESS != tx_timer_deactivate(&pMac->lim.limTimers.gLimQuietTimer))
    {
        limLog( pMac, LOGE,
            FL( "Unable to deactivate gLimQuietTimer! Will still attempt to re-activate anyway..." ));
    }

    // Set the NEW timeout value, in ticks
    if( TX_SUCCESS != tx_timer_change( &pMac->lim.limTimers.gLimQuietTimer,
                      SYS_MS_TO_TICKS(psessionEntry->gLimSpecMgmt.quietTimeoutValue), 0))
    {
        limLog( pMac, LOGE,
            FL( "Unable to change gLimQuietTimer! Will still attempt to re-activate anyway..." ));
    }

    pMac->lim.limTimers.gLimQuietTimer.sessionId = sessionId;
    if( TX_SUCCESS != tx_timer_activate(&pMac->lim.limTimers.gLimQuietTimer))
    {
        limLog( pMac, LOGE,
            FL("Unable to activate gLimQuietTimer! STA cannot honor Quiet BSS!"));
        limRestorePreQuietState(pMac, psessionEntry);

        psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_INIT;
        return;
    }
}


/** ------------------------------------------------------------------------ **/
/**
 * keep track of the number of ANI peers associated in the BSS
 * For the first and last ANI peer, we have to update EDCA params as needed
 *
 * When the first ANI peer joins the BSS, we notify SCH
 * When the last ANI peer leaves the BSS, we notify SCH
 */
void
limUtilCountStaAdd(
    tpAniSirGlobal pMac,
    tpDphHashNode  pSta,
    tpPESession psessionEntry)
{

    if ((! pSta) || (! pSta->valid) || (pSta->fAniCount))
        return;

    pSta->fAniCount = 1;

    if (pMac->lim.gLimNumOfAniSTAs++ != 0)
        return;

    // get here only if this is the first ANI peer in the BSS
    schEdcaProfileUpdate(pMac, psessionEntry);
}

void
limUtilCountStaDel(
    tpAniSirGlobal pMac,
    tpDphHashNode  pSta,
    tpPESession psessionEntry)
{

    if ((pSta == NULL) || (! pSta->fAniCount))
        return;

    /* Only if sta is invalid and the validInDummyState bit is set to 1,
     * then go ahead and update the count and profiles. This ensures
     * that the "number of ani station" count is properly incremented/decremented.
     */
    if (pSta->valid == 1)
         return;

    pSta->fAniCount = 0;

    if (pMac->lim.gLimNumOfAniSTAs <= 0)
    {
        limLog(pMac, LOGE, FL("CountStaDel: ignoring Delete Req when AniPeer count is %d"),
               pMac->lim.gLimNumOfAniSTAs);
        return;
    }

    pMac->lim.gLimNumOfAniSTAs--;

    if (pMac->lim.gLimNumOfAniSTAs != 0)
        return;

    // get here only if this is the last ANI peer in the BSS
    schEdcaProfileUpdate(pMac, psessionEntry);
}

/**
 * limSwitchChannelCback()
 *
 *FUNCTION:
 *  This is the callback function registered while requesting to switch channel
 *  after AP indicates a channel switch for spectrum management (11h).
 *
 *NOTE:
 * @param  pMac               Pointer to Global MAC structure
 * @param  status             Status of channel switch request
 * @param  data               User data
 * @param  psessionEntry      Session information
 * @return NONE
 */
void limSwitchChannelCback(tpAniSirGlobal pMac, eHalStatus status,
                           tANI_U32 *data, tpPESession psessionEntry)
{
   tSirMsgQ    mmhMsg = {0};
   tSirSmeSwitchChannelInd *pSirSmeSwitchChInd;

   psessionEntry->currentOperChannel = psessionEntry->currentReqChannel;

   /* We need to restore pre-channelSwitch state on the STA */
   if (limRestorePreChannelSwitchState(pMac, psessionEntry) != eSIR_SUCCESS)
   {
      limLog(pMac, LOGP, FL("Could not restore pre-channelSwitch (11h) state, resetting the system"));
      return;
   }

   mmhMsg.type = eWNI_SME_SWITCH_CHL_REQ;
   pSirSmeSwitchChInd = vos_mem_malloc(sizeof(tSirSmeSwitchChannelInd));
   if ( NULL == pSirSmeSwitchChInd )
   {
      limLog(pMac, LOGP, FL("Failed to allocate buffer for buffer descriptor"));
      return;
   }

   pSirSmeSwitchChInd->messageType = eWNI_SME_SWITCH_CHL_REQ;
   pSirSmeSwitchChInd->length = sizeof(tSirSmeSwitchChannelInd);
   pSirSmeSwitchChInd->newChannelId = psessionEntry->gLimChannelSwitch.primaryChannel;
   pSirSmeSwitchChInd->sessionId = psessionEntry->smeSessionId;
   vos_mem_copy( pSirSmeSwitchChInd->bssId, psessionEntry->bssId, sizeof(tSirMacAddr));
   mmhMsg.bodyptr = pSirSmeSwitchChInd;
   mmhMsg.bodyval = 0;

   MTRACE(macTrace(pMac, TRACE_CODE_TX_SME_MSG, psessionEntry->peSessionId,
                                                            mmhMsg.type));
   SysProcessMmhMsg(pMac, &mmhMsg);
}

/**
 * limSwitchPrimaryChannel()
 *
 *FUNCTION:
 *  This function changes the current operating channel
 *  and sets the new new channel ID in WNI_CFG_CURRENT_CHANNEL.
 *
 *NOTE:
 * @param  pMac        Pointer to Global MAC structure
 * @param  newChannel  new channel ID
 * @return NONE
 */
void limSwitchPrimaryChannel(tpAniSirGlobal pMac, tANI_U8 newChannel,tpPESession psessionEntry)
{
#if !defined WLAN_FEATURE_VOWIFI
    tANI_U32 localPwrConstraint;
#endif

    PELOG3(limLog(pMac, LOG3, FL("limSwitchPrimaryChannel: old chnl %d --> new chnl %d "),
           psessionEntry->currentOperChannel, newChannel);)
    psessionEntry->currentReqChannel = newChannel;
    psessionEntry->limRFBand = limGetRFBand(newChannel);

    psessionEntry->channelChangeReasonCode=LIM_SWITCH_CHANNEL_OPERATION;

    pMac->lim.gpchangeChannelCallback = limSwitchChannelCback;
    pMac->lim.gpchangeChannelData = NULL;

    psessionEntry->sub20_channelwidth =
         psessionEntry->lim_sub20_channel_switch_bandwidth;

#if defined WLAN_FEATURE_VOWIFI
    limSendSwitchChnlParams(pMac, newChannel, PHY_SINGLE_CHANNEL_CENTERED,
                            psessionEntry->maxTxPower,
                            psessionEntry->peSessionId,
                            VOS_FALSE);
#else
    if(wlan_cfgGetInt(pMac, WNI_CFG_LOCAL_POWER_CONSTRAINT, &localPwrConstraint) != eSIR_SUCCESS)
    {
        limLog( pMac, LOGP, FL( "Unable to read Local Power Constraint from cfg" ));
        return;
    }
    limSendSwitchChnlParams(pMac, newChannel, PHY_SINGLE_CHANNEL_CENTERED,
                            (tPowerdBm)localPwrConstraint,
                            psessionEntry->peSessionId,
                            VOS_FALSE);
#endif
    return;
}

/**
 * limSwitchPrimarySecondaryChannel()
 *
 *FUNCTION:
 *  This function changes the primary and secondary channel.
 *  If 11h is enabled and user provides a "new channel ID"
 *  that is different from the current operating channel,
 *  then we must set this new channel in WNI_CFG_CURRENT_CHANNEL,
 *  assign notify LIM of such change.
 *
 *NOTE:
 * @param  pMac        Pointer to Global MAC structure
 * @param  newChannel  New channel ID (or current channel ID)
 * @param  subband     CB secondary info:
 *                       - eANI_CB_SECONDARY_NONE
 *                       - eANI_CB_SECONDARY_UP
 *                       - eANI_CB_SECONDARY_DOWN
 * @return NONE
 */
void limSwitchPrimarySecondaryChannel(tpAniSirGlobal pMac, tpPESession psessionEntry, tANI_U8 newChannel, ePhyChanBondState subband)
{
#if !defined WLAN_FEATURE_VOWIFI
    tANI_U32 localPwrConstraint;
#endif

#if !defined WLAN_FEATURE_VOWIFI
    if(wlan_cfgGetInt(pMac, WNI_CFG_LOCAL_POWER_CONSTRAINT, &localPwrConstraint) != eSIR_SUCCESS) {
        limLog( pMac, LOGP, FL( "Unable to get Local Power Constraint from cfg" ));
        return;
    }
#endif

    /* Assign the callback to resume TX once channel is changed */
    psessionEntry->currentReqChannel = newChannel;
    psessionEntry->limRFBand = limGetRFBand(newChannel);

    psessionEntry->channelChangeReasonCode = LIM_SWITCH_CHANNEL_OPERATION;

    pMac->lim.gpchangeChannelCallback = limSwitchChannelCback;
    pMac->lim.gpchangeChannelData = NULL;

    psessionEntry->sub20_channelwidth =
         psessionEntry->lim_sub20_channel_switch_bandwidth;

#if defined WLAN_FEATURE_VOWIFI
                limSendSwitchChnlParams(pMac, newChannel, subband,
                                        psessionEntry->maxTxPower,
                                        psessionEntry->peSessionId,
                                        VOS_FALSE);
#else
                limSendSwitchChnlParams(pMac, newChannel, subband,
                                        (tPowerdBm)localPwrConstraint,
                                        psessionEntry->peSessionId,
                                        VOS_FALSE);
#endif

    // Store the new primary and secondary channel in session entries if different
    if (psessionEntry->currentOperChannel != newChannel)
    {
        limLog(pMac, LOGW,
            FL("switch old chnl %d --> new chnl %d "),
            psessionEntry->currentOperChannel, newChannel);
        psessionEntry->currentOperChannel = newChannel;
    }
    if (psessionEntry->htSecondaryChannelOffset != subband)
    {
        limLog(pMac, LOGW,
            FL("switch old sec chnl %d --> new sec chnl %d "),
            psessionEntry->htSecondaryChannelOffset, subband);
        psessionEntry->htSecondaryChannelOffset = subband;
        if (psessionEntry->htSecondaryChannelOffset == PHY_SINGLE_CHANNEL_CENTERED)
        {
            psessionEntry->htSupportedChannelWidthSet = WNI_CFG_CHANNEL_BONDING_MODE_DISABLE;
        }
        else
        {
            psessionEntry->htSupportedChannelWidthSet = WNI_CFG_CHANNEL_BONDING_MODE_ENABLE;
        }
        psessionEntry->htRecommendedTxWidthSet = psessionEntry->htSupportedChannelWidthSet;
    }

    return;
}


/**
 * limActiveScanAllowed()
 *
 *FUNCTION:
 * Checks if active scans are permitted on the given channel
 *
 *LOGIC:
 * The config variable SCAN_CONTROL_LIST contains pairs of (channelNum, activeScanAllowed)
 * Need to check if the channelNum matches, then depending on the corresponding
 * scan flag, return true (for activeScanAllowed==1) or false (otherwise).
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 *
 * @param  pMac       Pointer to Global MAC structure
 * @param  channelNum channel number
 * @return None
 */

tANI_U8 limActiveScanAllowed(
    tpAniSirGlobal pMac,
    tANI_U8             channelNum)
{
    tANI_U32 i;
    tANI_U8  channelPair[WNI_CFG_SCAN_CONTROL_LIST_LEN];
    tANI_U32 len = WNI_CFG_SCAN_CONTROL_LIST_LEN;
    if (wlan_cfgGetStr(pMac, WNI_CFG_SCAN_CONTROL_LIST, channelPair, &len)
        != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("Unable to get scan control list"));)
        return false;
    }

    if (len > WNI_CFG_SCAN_CONTROL_LIST_LEN)
    {
        limLog(pMac, LOGE, FL("Invalid scan control list length:%d"),
               len);
        return false;
    }

    for (i=0; (i+1) < len; i+=2)
    {
        if (channelPair[i] == channelNum)
            return ((channelPair[i+1] == eSIR_ACTIVE_SCAN) ? true : false);
    }
    return false;
}

/**
 * limTriggerBackgroundScanDuringQuietBss()
 *
 *FUNCTION:
 * This function is applicable to the STA only.
 * This function is called by limProcessQuietTimeout(),
 * when it is time to honor the Quiet BSS IE from the AP.
 *
 *LOGIC:
 * If 11H is enabled:
 * We cannot trigger a background scan. The STA needs to
 * shut-off Tx/Rx.
 * If 11 is not enabled:
 * Determine if the next channel that we are going to
 * scan is NOT the same channel (or not) on which the
 * Quiet BSS was requested.
 * If yes, then we cannot trigger a background scan on
 * this channel. Return with a false.
 * If no, then trigger a background scan. Return with
 * a true.
 *
 *ASSUMPTIONS:
 *
 *NOTE:
 * This API is redundant if the existing API,
 * limTriggerBackgroundScan(), were to return a valid
 * response instead of returning void.
 * If possible, try to revisit this API
 *
 * @param  pMac Pointer to Global MAC structure
 * @return eSIR_TRUE, if a background scan was attempted
 *         eSIR_FALSE, if not
 */
tAniBool limTriggerBackgroundScanDuringQuietBss( tpAniSirGlobal pMac )
{
    tAniBool bScanTriggered = eSIR_FALSE;
    tpPESession psessionEntry = &pMac->lim.gpSession[0];

    if (!LIM_IS_STA_ROLE(psessionEntry))
        return bScanTriggered;

    if( !psessionEntry->lim11hEnable )
    {
        tSirMacChanNum bgScanChannelList[WNI_CFG_BG_SCAN_CHANNEL_LIST_LEN];
        tANI_U32 len = WNI_CFG_BG_SCAN_CHANNEL_LIST_LEN;

        // Determine the next scan channel

        // Get background scan channel list from CFG
        if( eSIR_SUCCESS == wlan_cfgGetStr( pMac,
          WNI_CFG_BG_SCAN_CHANNEL_LIST,
          (tANI_U8 *) bgScanChannelList,
          (tANI_U32 *) &len ))
        {
            // Ensure that we do not go off scanning on the same
        // channel on which the Quiet BSS was requested
            if( psessionEntry->currentOperChannel!=
                bgScanChannelList[pMac->lim.gLimBackgroundScanChannelId] )
            {
            // For now, try and attempt a background scan. It will
            // be ideal if this API actually returns a success or
            // failure instead of having a void return type
            limTriggerBackgroundScan( pMac );

            bScanTriggered = eSIR_TRUE;
        }
        else
        {
            limLog( pMac, LOGW,
                FL("The next SCAN channel is the current operating channel on which a Quiet BSS is requested.! A background scan will not be triggered during this Quiet BSS period..."));
        }
    }
    else
    {
      limLog( pMac, LOGW,
          FL("Unable to retrieve WNI_CFG_VALID_CHANNEL_LIST from CFG! A background scan will not be triggered during this Quiet BSS period..."));
    }
  }
  return bScanTriggered;
}


/**
 * limGetHTCapability()
 *
 *FUNCTION:
 * A utility function that returns the "current HT capability state" for the HT
 * capability of interest (as requested in the API)
 *
 *LOGIC:
 * This routine will return with the "current" setting of a requested HT
 * capability. This state info could be retrieved from -
 * a) CFG (for static entries)
 * b) Run time info
 *   - Dynamic state maintained by LIM
 *   - Configured at radio init time by SME
 *
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 *
 * @param  pMac  Pointer to Global MAC structure
 * @param  htCap The HT capability being queried
 * @return tANI_U8 The current state of the requested HT capability is returned in a
 *            tANI_U8 variable
 */

tANI_U8 limGetHTCapability( tpAniSirGlobal pMac,
        tANI_U32 htCap, tpPESession psessionEntry)
{
tANI_U8 retVal = 0;
tANI_U8 *ptr;
tANI_U32  cfgValue;
tSirMacHTCapabilityInfo macHTCapabilityInfo = {0};
tSirMacExtendedHTCapabilityInfo macExtHTCapabilityInfo = {0};
tSirMacTxBFCapabilityInfo macTxBFCapabilityInfo = {0};
tSirMacASCapabilityInfo macASCapabilityInfo = {0};

  //
  // Determine which CFG to read from. Not ALL of the HT
  // related CFG's need to be read each time this API is
  // accessed
  //
  if( htCap >= eHT_ANTENNA_SELECTION &&
      htCap < eHT_SI_GRANULARITY )
  {
    /* Get Antenna Selection HT Capabilities */
    if( eSIR_SUCCESS != wlan_cfgGetInt( pMac, WNI_CFG_AS_CAP, &cfgValue ))
      cfgValue = 0;
    ptr = (tANI_U8 *) &macASCapabilityInfo;
    *((tANI_U8 *)ptr) =  (tANI_U8) (cfgValue & 0xff);
  }
  else
  {
    if( htCap >= eHT_TX_BEAMFORMING &&
        htCap < eHT_ANTENNA_SELECTION )
    {
      // Get Transmit Beam Forming HT Capabilities
      if( eSIR_SUCCESS != wlan_cfgGetInt( pMac, WNI_CFG_TX_BF_CAP, &cfgValue ))
        cfgValue = 0;
      ptr = (tANI_U8 *) &macTxBFCapabilityInfo;
      *((tANI_U32 *)ptr) =  (tANI_U32) (cfgValue);
    }
    else
    {
      if( htCap >= eHT_PCO &&
          htCap < eHT_TX_BEAMFORMING )
      {
        // Get Extended HT Capabilities
        if( eSIR_SUCCESS != wlan_cfgGetInt( pMac, WNI_CFG_EXT_HT_CAP_INFO, &cfgValue ))
          cfgValue = 0;
        ptr = (tANI_U8 *) &macExtHTCapabilityInfo;
        *((tANI_U16 *)ptr) =  (tANI_U16) (cfgValue & 0xffff);
      }
      else
      {
        if( htCap < eHT_MAX_RX_AMPDU_FACTOR )
        {
          // Get HT Capabilities
          if( eSIR_SUCCESS != wlan_cfgGetInt( pMac, WNI_CFG_HT_CAP_INFO, &cfgValue ))
            cfgValue = 0;
          ptr = (tANI_U8 *) &macHTCapabilityInfo;
          // CR 265282 MDM SoftAP 2.4PL: SoftAP boot up crash in 2.4 PL builds while same WLAN SU is working on 2.1 PL
          *ptr++ = cfgValue & 0xff;
          *ptr = (cfgValue >> 8) & 0xff;
        }
      }
    }
  }

  switch( htCap )
  {
    case eHT_LSIG_TXOP_PROTECTION:
      retVal = pMac->lim.gHTLsigTXOPProtection;
      break;

    case eHT_STBC_CONTROL_FRAME:
      retVal = (tANI_U8) macHTCapabilityInfo.stbcControlFrame;
      break;

    case eHT_PSMP:
      retVal = pMac->lim.gHTPSMPSupport;
      break;

    case eHT_DSSS_CCK_MODE_40MHZ:
      retVal = pMac->lim.gHTDsssCckRate40MHzSupport;
      break;

    case eHT_MAX_AMSDU_LENGTH:
      retVal = (tANI_U8) macHTCapabilityInfo.maximalAMSDUsize;
      break;

    case eHT_MAX_AMSDU_NUM:
      retVal = (tANI_U8) psessionEntry->max_amsdu_num;
      break;

    case eHT_DELAYED_BA:
      retVal = (tANI_U8) macHTCapabilityInfo.delayedBA;
      break;

    case eHT_RX_STBC:
      retVal = (tANI_U8) psessionEntry->htConfig.ht_rx_stbc;
      break;

    case eHT_TX_STBC:
      retVal = (tANI_U8) psessionEntry->htConfig.ht_tx_stbc;
      break;

    case eHT_SHORT_GI_40MHZ:
      retVal =(tANI_U8)
      (psessionEntry->htConfig.ht_sgi)? macHTCapabilityInfo.shortGI40MHz : 0;
      break;

    case eHT_SHORT_GI_20MHZ:
      retVal = (tANI_U8)
      (psessionEntry->htConfig.ht_sgi)? macHTCapabilityInfo.shortGI20MHz : 0;
      break;

    case eHT_GREENFIELD:
      retVal = (tANI_U8) macHTCapabilityInfo.greenField;
      break;

    case eHT_MIMO_POWER_SAVE:
      retVal = (tANI_U8) pMac->lim.gHTMIMOPSState;
      break;

    case eHT_SUPPORTED_CHANNEL_WIDTH_SET:
      retVal = (tANI_U8) psessionEntry->htSupportedChannelWidthSet;
      break;

    case eHT_ADVANCED_CODING:
      retVal = (tANI_U8) psessionEntry->htConfig.ht_rx_ldpc;
      break;

    case eHT_MAX_RX_AMPDU_FACTOR:
      retVal = pMac->lim.gHTMaxRxAMpduFactor;
      break;

    case eHT_MPDU_DENSITY:
      retVal = pMac->lim.gHTAMpduDensity;
      break;

    case eHT_PCO:
      retVal = (tANI_U8) macExtHTCapabilityInfo.pco;
      break;

    case eHT_TRANSITION_TIME:
      retVal = (tANI_U8) macExtHTCapabilityInfo.transitionTime;
      break;

    case eHT_MCS_FEEDBACK:
      retVal = (tANI_U8) macExtHTCapabilityInfo.mcsFeedback;
      break;

    case eHT_TX_BEAMFORMING:
      retVal = (tANI_U8) macTxBFCapabilityInfo.txBF;
      break;

    case eHT_ANTENNA_SELECTION:
      retVal = (tANI_U8) macASCapabilityInfo.antennaSelection;
      break;

    case eHT_SI_GRANULARITY:
      retVal = pMac->lim.gHTServiceIntervalGranularity;
      break;

    case eHT_CONTROLLED_ACCESS:
      retVal = pMac->lim.gHTControlledAccessOnly;
      break;

    case eHT_RIFS_MODE:
      retVal = psessionEntry->beaconParams.fRIFSMode;
      break;

    case eHT_RECOMMENDED_TX_WIDTH_SET:
      retVal = psessionEntry->htRecommendedTxWidthSet;
      break;

    case eHT_EXTENSION_CHANNEL_OFFSET:
      retVal = psessionEntry->htSecondaryChannelOffset;
      break;

    case eHT_OP_MODE:
      if (LIM_IS_AP_ROLE(psessionEntry))
          retVal = psessionEntry->htOperMode;
      else
          retVal = pMac->lim.gHTOperMode;
      break;

    case eHT_BASIC_STBC_MCS:
      retVal = pMac->lim.gHTSTBCBasicMCS;
      break;

    case eHT_DUAL_CTS_PROTECTION:
      retVal = pMac->lim.gHTDualCTSProtection;
      break;

    case eHT_LSIG_TXOP_PROTECTION_FULL_SUPPORT:
      retVal = psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport;
      break;

    case eHT_PCO_ACTIVE:
      retVal = pMac->lim.gHTPCOActive;
      break;

    case eHT_PCO_PHASE:
      retVal = pMac->lim.gHTPCOPhase;
      break;

    default:
      break;
  }

  return retVal;
}

void limGetMyMacAddr(tpAniSirGlobal pMac, tANI_U8 *mac)
{
    vos_mem_copy( mac, pMac->lim.gLimMyMacAddr, sizeof(tSirMacAddr));
    return;
}




/** -------------------------------------------------------------
\fn limEnable11aProtection
\brief based on config setting enables\disables 11a protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnable11aProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(NULL == psessionEntry)
    {
        PELOG3(limLog(pMac, LOG3, FL("psessionEntry is NULL"));)
        return eSIR_FAILURE;
    }
        //overlapping protection configuration check.
        if (overlap) {
        } else {
            //normal protection config check
            if (LIM_IS_AP_ROLE(psessionEntry) &&
                (!psessionEntry->cfgProtection.fromlla)) {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL("protection from 11a is disabled"));)
                return eSIR_SUCCESS;
            }
        }

    if (enable)
    {
        //If we are AP and HT capable, we need to set the HT OP mode
        //appropriately.
        if ((LIM_IS_AP_ROLE(psessionEntry) ||
             LIM_IS_BT_AMP_AP_ROLE(psessionEntry)) &&
             (true == psessionEntry->htCapability)) {
            if(overlap)
            {
                pMac->lim.gLimOverlap11aParams.protectionEnabled = true;
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != pMac->lim.gHTOperMode) &&
                   (eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode))
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);
                }
            }
            else
            {
                psessionEntry->gLim11aParams.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode)
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_MIXED;
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_MIXED;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);

                }
            }
        }

        /* This part is common for station as well. */
        if(false == psessionEntry->beaconParams.llaCoexist)
        {
            PELOG1(limLog(pMac, LOG1, FL(" => protection from 11A Enabled"));)
            pBeaconParams->llaCoexist = psessionEntry->beaconParams.llaCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_llACOEXIST_CHANGED;
        }
    }
    else if (true == psessionEntry->beaconParams.llaCoexist)
    {
        //for AP role.
        //we need to take care of HT OP mode change if needed.
        //We need to take care of Overlap cases.
        if (LIM_IS_AP_ROLE(psessionEntry)) {
            if (overlap) {
                //Overlap Legacy protection disabled.
                pMac->lim.gLimOverlap11aParams.protectionEnabled = false;

                /* We need to take care of HT OP mode if we are HT AP. */
                if(psessionEntry->htCapability)
                {
                   // no HT op mode change if any of the overlap protection enabled.
                    if(!(pMac->lim.gLimOverlap11aParams.protectionEnabled ||
                         pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                         pMac->lim.gLimOverlapNonGfParams.protectionEnabled))

                    {
                        //Check if there is a need to change HT OP mode.
                        if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == pMac->lim.gHTOperMode)
                        {
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                            limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);

                            if(psessionEntry->gLimHt20Params.protectionEnabled)
                                pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            else
                                pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                        }
                    }
                }
            }
            else
            {
                //Disable protection from 11A stations.
                psessionEntry->gLim11aParams.protectionEnabled = false;
                limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);

                //Check if any other non-HT protection enabled.
                //Right now we are in HT OP Mixed mode.
                //Change HT op mode appropriately.

                //Change HT OP mode to 01 if any overlap protection enabled
                if(pMac->lim.gLimOverlap11aParams.protectionEnabled ||
                   pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                   pMac->lim.gLimOverlapNonGfParams.protectionEnabled)

                {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                }
                else if(psessionEntry->gLimHt20Params.protectionEnabled)
                {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                }
                else
                {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                }
            }
        if(!pMac->lim.gLimOverlap11aParams.protectionEnabled &&
           !psessionEntry->gLim11aParams.protectionEnabled)
            {
                PELOG1(limLog(pMac, LOG1, FL("===> Protection from 11A Disabled"));)
                pBeaconParams->llaCoexist = psessionEntry->beaconParams.llaCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llACOEXIST_CHANGED;
            }
        }
        //for station role
        else
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from 11A Disabled"));)
            pBeaconParams->llaCoexist = psessionEntry->beaconParams.llaCoexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_llACOEXIST_CHANGED;
        }
    }

    return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limEnable11gProtection
\brief based on config setting enables\disables 11g protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/

tSirRetStatus
limEnable11gProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    //overlapping protection configuration check.
    if (overlap) {
    } else {
        //normal protection config check
        if (LIM_IS_AP_ROLE(psessionEntry) &&
                !psessionEntry->cfgProtection.fromllb) {
            // protection disabled.
            PELOG1(limLog(pMac, LOG1, FL("protection from 11b is disabled"));)
            return eSIR_SUCCESS;
        } else if (!LIM_IS_AP_ROLE(psessionEntry)) {
            if(!pMac->lim.cfgProtection.fromllb) {
                // protection disabled.
                PELOG1(limLog(pMac, LOG1, FL("protection from 11b is disabled"));)
                return eSIR_SUCCESS;
            }
        }
    }

    if (enable)
    {
        //If we are AP and HT capable, we need to set the HT OP mode
        //appropriately.
        if (LIM_IS_AP_ROLE(psessionEntry)) {
            if(overlap)
            {
                psessionEntry->gLimOlbcParams.protectionEnabled = true;
                PELOGE(limLog(pMac, LOG1, FL("protection from olbc is enabled"));)
                if(true == psessionEntry->htCapability)
                {
                    if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != psessionEntry->htOperMode) &&
                            (eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode))
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    }
                    //CR-263021: OBSS bit is not switching back to 0 after disabling the overlapping legacy BSS
                    // This fixes issue of OBSS bit not set after 11b, 11g station leaves
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    //Not processing OBSS bit from other APs, as we are already taking care
                    //of Protection from overlapping BSS based on erp IE or useProtection bit
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams, psessionEntry);
                }
            }
            else
            {
                psessionEntry->gLim11bParams.protectionEnabled = true;
                PELOGE(limLog(pMac, LOG1, FL("protection from 11b is enabled"));)
                if(true == psessionEntry->htCapability)
                {
                    if(eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode)
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_MIXED;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                        limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);
                    }
                }
            }
        }else if (LIM_IS_BT_AMP_AP_ROLE(psessionEntry) &&
                (true == psessionEntry->htCapability)) {
                if(overlap)
                {
                    psessionEntry->gLimOlbcParams.protectionEnabled = true;
                    if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != pMac->lim.gHTOperMode) &&
                            (eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode))
                    {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    }
                    //CR-263021: OBSS bit is not switching back to 0 after disabling the overlapping legacy BSS
                    // This fixes issue of OBSS bit not set after 11b, 11g station leaves
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    //Not processing OBSS bit from other APs, as we are already taking care
                    //of Protection from overlapping BSS based on erp IE or useProtection bit
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams, psessionEntry);
                }
                else
                {
                    psessionEntry->gLim11bParams.protectionEnabled = true;
                    if(eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode)
                    {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_MIXED;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                        limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);
                    }
                }
            }

        /* This part is common for station as well. */
        if(false == psessionEntry->beaconParams.llbCoexist)
        {
            PELOG1(limLog(pMac, LOG1, FL("=> 11G Protection Enabled"));)
            pBeaconParams->llbCoexist = psessionEntry->beaconParams.llbCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_llBCOEXIST_CHANGED;
        }
    }
    else if (true == psessionEntry->beaconParams.llbCoexist)
    {
        //for AP role.
        //we need to take care of HT OP mode change if needed.
        //We need to take care of Overlap cases.
        if (LIM_IS_AP_ROLE(psessionEntry)) {
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                psessionEntry->gLimOlbcParams.protectionEnabled = false;

                //We need to take care of HT OP mode if we are HT AP.
                if(psessionEntry->htCapability)
                {
                    // no HT op mode change if any of the overlap protection enabled.
                    if(!(psessionEntry->gLimOverlap11gParams.protectionEnabled ||
                                psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                                psessionEntry->gLimOverlapNonGfParams.protectionEnabled))
                    {
                        //Check if there is a need to change HT OP mode.
                        if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == psessionEntry->htOperMode)
                        {
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                            limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                            if (psessionEntry->gLimHt20Params.protectionEnabled) {
                                if (eHT_CHANNEL_WIDTH_20MHZ ==
                                      psessionEntry->htSupportedChannelWidthSet)
                                    psessionEntry->htOperMode =
                                        eSIR_HT_OP_MODE_PURE;
                                else
                                    psessionEntry->htOperMode =
                                        eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            } else
                                psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        }
                    }
                }
            }
            else
            {
                //Disable protection from 11B stations.
                psessionEntry->gLim11bParams.protectionEnabled = false;
                PELOGE(limLog(pMac, LOG1, FL("===> 11B Protection Disabled"));)
                    //Check if any other non-HT protection enabled.
                if(!psessionEntry->gLim11gParams.protectionEnabled)
                {
                    //Right now we are in HT OP Mixed mode.
                    //Change HT op mode appropriately.
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);

                    //Change HT OP mode to 01 if any overlap protection enabled
                    if(psessionEntry->gLimOlbcParams.protectionEnabled ||
                            psessionEntry->gLimOverlap11gParams.protectionEnabled ||
                            psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                            psessionEntry->gLimOverlapNonGfParams.protectionEnabled)
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        PELOGE(limLog(pMac, LOG1, FL("===> 11G Protection Disabled"));)
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    }
                    else if(psessionEntry->gLimHt20Params.protectionEnabled)
                    {
                        if(eHT_CHANNEL_WIDTH_20MHZ ==
                              psessionEntry->htSupportedChannelWidthSet)
                              psessionEntry->htOperMode =
                                   eSIR_HT_OP_MODE_PURE;
                        else
                              psessionEntry->htOperMode =
                                   eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        PELOGE(limLog(pMac, LOG1, FL("===> 11G Protection Disabled"));)
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                    else
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                }
            }
            if(!psessionEntry->gLimOlbcParams.protectionEnabled &&
                    !psessionEntry->gLim11bParams.protectionEnabled)
            {
                PELOGE(limLog(pMac, LOG1, FL("===> 11G Protection Disabled"));)
                pBeaconParams->llbCoexist = psessionEntry->beaconParams.llbCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llBCOEXIST_CHANGED;
            }
        }else if(LIM_IS_BT_AMP_AP_ROLE(psessionEntry)) {
                if(overlap)
                {
                    //Overlap Legacy protection disabled.
                psessionEntry->gLimOlbcParams.protectionEnabled = false;

                    /* We need to take care of HT OP mode if we are HT AP. */
                    if(psessionEntry->htCapability)
                    {
                        // no HT op mode change if any of the overlap protection enabled.
                        if(!(pMac->lim.gLimOverlap11gParams.protectionEnabled ||
                                    pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                                    pMac->lim.gLimOverlapNonGfParams.protectionEnabled))

                        {
                            //Check if there is a need to change HT OP mode.
                            if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == pMac->lim.gHTOperMode)
                            {
                                limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                                limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                            if(psessionEntry->gLimHt20Params.protectionEnabled)
                                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                                else
                                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                            }
                        }
                    }
                }
                else
                {
                    //Disable protection from 11B stations.
                psessionEntry->gLim11bParams.protectionEnabled = false;
                    //Check if any other non-HT protection enabled.
                if(!psessionEntry->gLim11gParams.protectionEnabled)
                    {
                        //Right now we are in HT OP Mixed mode.
                        //Change HT op mode appropriately.
                        limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);

                        //Change HT OP mode to 01 if any overlap protection enabled
                    if(psessionEntry->gLimOlbcParams.protectionEnabled ||
                                pMac->lim.gLimOverlap11gParams.protectionEnabled ||
                                pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                                pMac->lim.gLimOverlapNonGfParams.protectionEnabled)

                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                            limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                        }
                    else if(psessionEntry->gLimHt20Params.protectionEnabled)
                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                        }
                        else
                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                        }
                    }
                }
            if(!psessionEntry->gLimOlbcParams.protectionEnabled &&
                  !psessionEntry->gLim11bParams.protectionEnabled)
                {
                    PELOG1(limLog(pMac, LOG1, FL("===> 11G Protection Disabled"));)
                pBeaconParams->llbCoexist = psessionEntry->beaconParams.llbCoexist = false;
                    pBeaconParams->paramChangeBitmap |= PARAM_llBCOEXIST_CHANGED;
                }
            }
        //for station role
            else
            {
                PELOG1(limLog(pMac, LOG1, FL("===> 11G Protection Disabled"));)
            pBeaconParams->llbCoexist = psessionEntry->beaconParams.llbCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llBCOEXIST_CHANGED;
            }
    }
    return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limEnableHtProtectionFrom11g
\brief based on cofig enables\disables protection from 11g.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHtProtectionFrom11g(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // protection from 11g is only for HT stations.

    //overlapping protection configuration check.
    if (overlap) {
        if (LIM_IS_AP_ROLE(psessionEntry) &&
           (!psessionEntry->cfgProtection.overlapFromllg)) {
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL("overlap protection from 11g is disabled")););
            return eSIR_SUCCESS;
        } else if (LIM_IS_BT_AMP_AP_ROLE(psessionEntry) &&
                  (!pMac->lim.cfgProtection.overlapFromllg)) {
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL("overlap protection from 11g is disabled")););
            return eSIR_SUCCESS;
        }
    } else {
        //normal protection config check
       if (LIM_IS_AP_ROLE(psessionEntry) &&
           !psessionEntry->cfgProtection.fromllg) {
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL("protection from 11g is disabled"));)
            return eSIR_SUCCESS;
       } else if(!LIM_IS_AP_ROLE(psessionEntry)) {
           if (!pMac->lim.cfgProtection.fromllg) {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL("protection from 11g is disabled"));)
                return eSIR_SUCCESS;
           }
       }
    }

    if (enable) {
        //If we are AP and HT capable, we need to set the HT OP mode
        //appropriately.

        if (LIM_IS_AP_ROLE(psessionEntry)) {
            if(overlap)
            {
                psessionEntry->gLimOverlap11gParams.protectionEnabled = true;
                //11g exists in overlap BSS.
                //need not to change the operating mode to overlap_legacy
                //if higher or same protection operating mode is enabled right now.
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != psessionEntry->htOperMode) &&
                    (eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode))
                {
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                }
                limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams, psessionEntry);
            }
            else
            {
                //11g is associated to an AP operating in 11n mode.
                //Change the HT operating mode to 'mixed mode'.
                psessionEntry->gLim11gParams.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode)
                {
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_MIXED;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);
                }
            }
        } else if(LIM_IS_BT_AMP_AP_ROLE(psessionEntry)) {
            if(overlap)
            {
                pMac->lim.gLimOverlap11gParams.protectionEnabled = true;
                //11g exists in overlap BSS.
                //need not to change the operating mode to overlap_legacy
                //if higher or same protection operating mode is enabled right now.
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != pMac->lim.gHTOperMode) &&
                    (eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode))
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                }
            }
            else
            {
                //11g is associated to an AP operating in 11n mode.
                //Change the HT operating mode to 'mixed mode'.
                psessionEntry->gLim11gParams.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode)
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_MIXED;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  true, overlap, pBeaconParams,psessionEntry);
                }
            }
        }

        /* This part is common for station as well. */
        if(false == psessionEntry->beaconParams.llgCoexist)
        {
            pBeaconParams->llgCoexist = psessionEntry->beaconParams.llgCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
        }
        else if (true == psessionEntry->gLimOverlap11gParams.protectionEnabled)
        {
            // As operating mode changed after G station assoc some way to update beacon
            // This addresses the issue of mode not changing to - 11 in beacon when OBSS overlap is enabled
            //pMac->sch.schObject.fBeaconChanged = 1;
            pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
        }
    }
    else if (true == psessionEntry->beaconParams.llgCoexist)
    {
        //for AP role.
        //we need to take care of HT OP mode change if needed.
        //We need to take care of Overlap cases.

        if (LIM_IS_AP_ROLE(psessionEntry)) {
            if (overlap) {
                //Overlap Legacy protection disabled.
                if (psessionEntry->gLim11gParams.numSta == 0)
                psessionEntry->gLimOverlap11gParams.protectionEnabled = false;

                // no HT op mode change if any of the overlap protection enabled.
                if(!(psessionEntry->gLimOlbcParams.protectionEnabled ||
                    psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                    psessionEntry->gLimOverlapNonGfParams.protectionEnabled))
                {
                    //Check if there is a need to change HT OP mode.
                    if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == psessionEntry->htOperMode)
                    {
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                        limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);

                        if(psessionEntry->gLimHt20Params.protectionEnabled){
                            if(eHT_CHANNEL_WIDTH_20MHZ ==
                                 psessionEntry->htSupportedChannelWidthSet)
                                 psessionEntry->htOperMode =
                                      eSIR_HT_OP_MODE_PURE;
                            else
                                 psessionEntry->htOperMode =
                                      eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        }
                        else
                            psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                    }
                }
            }
            else
            {
                //Disable protection from 11G stations.
                psessionEntry->gLim11gParams.protectionEnabled = false;
                //Check if any other non-HT protection enabled.
                if(!psessionEntry->gLim11bParams.protectionEnabled)
                {

                    //Right now we are in HT OP Mixed mode.
                    //Change HT op mode appropriately.
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);

                    //Change HT OP mode to 01 if any overlap protection enabled
                    if(psessionEntry->gLimOlbcParams.protectionEnabled ||
                        psessionEntry->gLimOverlap11gParams.protectionEnabled ||
                        psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                        psessionEntry->gLimOverlapNonGfParams.protectionEnabled)

                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    }
                    else if(psessionEntry->gLimHt20Params.protectionEnabled)
                    {
                        if(eHT_CHANNEL_WIDTH_20MHZ ==
                              psessionEntry->htSupportedChannelWidthSet)
                              psessionEntry->htOperMode =
                                   eSIR_HT_OP_MODE_PURE;
                        else
                              psessionEntry->htOperMode =
                                   eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                    else
                    {
                        psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                }
            }
            if(!psessionEntry->gLimOverlap11gParams.protectionEnabled &&
                  !psessionEntry->gLim11gParams.protectionEnabled)
            {
                limLog(pMac, LOG1, FL("===> Protection from 11G Disabled"));
                pBeaconParams->llgCoexist = psessionEntry->beaconParams.llgCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
            }
        } else if(LIM_IS_BT_AMP_AP_ROLE(psessionEntry)) {
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                pMac->lim.gLimOverlap11gParams.protectionEnabled = false;

                // no HT op mode change if any of the overlap protection enabled.
                if(!(psessionEntry->gLimOlbcParams.protectionEnabled ||
                    psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                    psessionEntry->gLimOverlapNonGfParams.protectionEnabled))
                {
                    //Check if there is a need to change HT OP mode.
                    if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == pMac->lim.gHTOperMode)
                    {
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                        limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);

                        if(psessionEntry->gLimHt20Params.protectionEnabled)
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        else
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                    }
                }
            }
            else
            {
                //Disable protection from 11G stations.
                psessionEntry->gLim11gParams.protectionEnabled = false;
                //Check if any other non-HT protection enabled.
                if(!psessionEntry->gLim11bParams.protectionEnabled)
                {

                    //Right now we are in HT OP Mixed mode.
                    //Change HT op mode appropriately.
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);

                    //Change HT OP mode to 01 if any overlap protection enabled
                    if(psessionEntry->gLimOlbcParams.protectionEnabled ||
                        pMac->lim.gLimOverlap11gParams.protectionEnabled ||
                        pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                        pMac->lim.gLimOverlapNonGfParams.protectionEnabled)

                    {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                        limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                    }
                    else if(psessionEntry->gLimHt20Params.protectionEnabled)
                    {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                    else
                    {
                        pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                        limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    }
                }
            }
            if(!pMac->lim.gLimOverlap11gParams.protectionEnabled &&
                  !psessionEntry->gLim11gParams.protectionEnabled)
            {
                PELOG1(limLog(pMac, LOG1, FL("===> Protection from 11G Disabled"));)
                pBeaconParams->llgCoexist = psessionEntry->beaconParams.llgCoexist = false;
                pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
            }
        }
        //for station role
        else
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from 11G Disabled"));)
            pBeaconParams->llgCoexist = psessionEntry->beaconParams.llgCoexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_llGCOEXIST_CHANGED;
        }
    }
    return eSIR_SUCCESS;
}
//FIXME_PROTECTION : need to check for no APSD whenever we want to enable this protection.
//This check will be done at the caller.

/** -------------------------------------------------------------
\fn limEnableHtObssProtection
\brief based on cofig enables\disables obss protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHtOBSSProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{


    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.

    //overlapping protection configuration check.
    if(overlap)
    {
        //overlapping protection configuration check.
    }
    else
    {
        //normal protection config check
        if (LIM_IS_AP_ROLE(psessionEntry) &&
            !psessionEntry->cfgProtection.obss) { //ToDo Update this field
            // protection disabled.
            PELOG1(limLog(pMac, LOG1, FL("protection from Obss is disabled"));)
            return eSIR_SUCCESS;
        } else if (!LIM_IS_AP_ROLE(psessionEntry)) {
            if (!pMac->lim.cfgProtection.obss) { //ToDo Update this field
                // protection disabled.
                PELOG1(limLog(pMac, LOG1, FL("protection from Obss is disabled"));)
                return eSIR_SUCCESS;
            }
        }
    }


    if (LIM_IS_AP_ROLE(psessionEntry)) {
        if ((enable) && (false == psessionEntry->beaconParams.gHTObssMode) )
        {
            PELOG1(limLog(pMac, LOG1, FL("=>obss protection enabled"));)
            psessionEntry->beaconParams.gHTObssMode = true;
            pBeaconParams->paramChangeBitmap |= PARAM_OBSS_MODE_CHANGED; // UPDATE AN ENUM FOR OBSS MODE <todo>

         }
         else if (!enable && (true == psessionEntry->beaconParams.gHTObssMode))
         {
            PELOG1(limLog(pMac, LOG1, FL("===> obss Protection disabled"));)
            psessionEntry->beaconParams.gHTObssMode = false;
            pBeaconParams->paramChangeBitmap |= PARAM_OBSS_MODE_CHANGED;

         }
//CR-263021: OBSS bit is not switching back to 0 after disabling the overlapping legacy BSS
         if (!enable && !overlap)
         {
             psessionEntry->gLimOverlap11gParams.protectionEnabled = false;
         }
    } else
    {
        if ((enable) && (false == psessionEntry->beaconParams.gHTObssMode) )
        {
            PELOG1(limLog(pMac, LOG1, FL("=>obss protection enabled"));)
            psessionEntry->beaconParams.gHTObssMode = true;
            pBeaconParams->paramChangeBitmap |= PARAM_OBSS_MODE_CHANGED; // UPDATE AN ENUM FOR OBSS MODE <todo>

        }
        else if (!enable && (true == psessionEntry->beaconParams.gHTObssMode))
        {

            PELOG1(limLog(pMac, LOG1, FL("===> obss Protection disabled"));)
            psessionEntry->beaconParams.gHTObssMode = false;
            pBeaconParams->paramChangeBitmap |= PARAM_OBSS_MODE_CHANGED;

        }
    }
    return eSIR_SUCCESS;
}
/** -------------------------------------------------------------
\fn limEnableHT20Protection
\brief based on cofig enables\disables protection from Ht20.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHT20Protection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.

    //overlapping protection configuration check.
    if(overlap) {
    } else {
        //normal protection config check
        if (LIM_IS_AP_ROLE(psessionEntry) &&
            !psessionEntry->cfgProtection.ht20) {
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL("protection from HT20 is disabled"));)
            return eSIR_SUCCESS;
        } else if (!LIM_IS_AP_ROLE(psessionEntry)) {
            if (!pMac->lim.cfgProtection.ht20) {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL("protection from HT20 is disabled"));)
                return eSIR_SUCCESS;
            }
        }
    }

    if (enable) {
        //If we are AP and HT capable, we need to set the HT OP mode
        //appropriately.

        if (LIM_IS_AP_ROLE(psessionEntry)) {
            if(overlap)
            {
                psessionEntry->gLimOverlapHt20Params.protectionEnabled = true;
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != psessionEntry->htOperMode) &&
                    (eSIR_HT_OP_MODE_MIXED != psessionEntry->htOperMode))
                {
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                }
            }
            else
            {
               psessionEntry->gLimHt20Params.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_PURE == psessionEntry->htOperMode)
                {
                    if (psessionEntry->htSupportedChannelWidthSet !=
                            eHT_CHANNEL_WIDTH_20MHZ)
                        psessionEntry->htOperMode =
                            eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                    limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                }
            }
        } else if(LIM_IS_BT_AMP_AP_ROLE(psessionEntry)) {
            if(overlap)
            {
                pMac->lim.gLimOverlapHt20Params.protectionEnabled = true;
                if((eSIR_HT_OP_MODE_OVERLAP_LEGACY != pMac->lim.gHTOperMode) &&
                    (eSIR_HT_OP_MODE_MIXED != pMac->lim.gHTOperMode))
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_OVERLAP_LEGACY;
                    limEnableHtRifsProtection(pMac, true, overlap, pBeaconParams,psessionEntry);
                }
            }
            else
            {
                psessionEntry->gLimHt20Params.protectionEnabled = true;
                if(eSIR_HT_OP_MODE_PURE == pMac->lim.gHTOperMode)
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                    limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                }
            }
        }

        /* This part is common for station as well. */
        if(false == psessionEntry->beaconParams.ht20Coexist)
        {
            PELOG1(limLog(pMac, LOG1, FL("=> Protection from HT20 Enabled"));)
            pBeaconParams->ht20MhzCoexist = psessionEntry->beaconParams.ht20Coexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_HT20MHZCOEXIST_CHANGED;
        }
    }
    else if (true == psessionEntry->beaconParams.ht20Coexist)
    {
        //for AP role.
        //we need to take care of HT OP mode change if needed.
        //We need to take care of Overlap cases.
        if (LIM_IS_AP_ROLE(psessionEntry)) {
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                psessionEntry->gLimOverlapHt20Params.protectionEnabled = false;

                // no HT op mode change if any of the overlap protection enabled.
                if(!(psessionEntry->gLimOlbcParams.protectionEnabled ||
                    psessionEntry->gLimOverlap11gParams.protectionEnabled ||
                    psessionEntry->gLimOverlapHt20Params.protectionEnabled ||
                    psessionEntry->gLimOverlapNonGfParams.protectionEnabled))
                {

                    //Check if there is a need to change HT OP mode.
                    if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == psessionEntry->htOperMode)
                    {
                        if(psessionEntry->gLimHt20Params.protectionEnabled)
                        {
                            if (psessionEntry->htSupportedChannelWidthSet ==
                                    eHT_CHANNEL_WIDTH_20MHZ)
                               psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                            else
                               psessionEntry->htOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                            limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                        }
                        else
                        {
                            psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                        }
                    }
                }
            }
            else
            {
                //Disable protection from 11G stations.
                psessionEntry->gLimHt20Params.protectionEnabled = false;

                //Change HT op mode appropriately.
                if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == psessionEntry->htOperMode)
                {
                    psessionEntry->htOperMode = eSIR_HT_OP_MODE_PURE;
                    limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                }
            }
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from HT 20 Disabled"));)
            pBeaconParams->ht20MhzCoexist = psessionEntry->beaconParams.ht20Coexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_HT20MHZCOEXIST_CHANGED;
        } else if(LIM_IS_BT_AMP_AP_ROLE(psessionEntry)) {
            if(overlap)
            {
                //Overlap Legacy protection disabled.
                pMac->lim.gLimOverlapHt20Params.protectionEnabled = false;

                // no HT op mode change if any of the overlap protection enabled.
                if(!(psessionEntry->gLimOlbcParams.protectionEnabled ||
                    pMac->lim.gLimOverlap11gParams.protectionEnabled ||
                    pMac->lim.gLimOverlapHt20Params.protectionEnabled ||
                    pMac->lim.gLimOverlapNonGfParams.protectionEnabled))
                {

                    //Check if there is a need to change HT OP mode.
                    if(eSIR_HT_OP_MODE_OVERLAP_LEGACY == pMac->lim.gHTOperMode)
                    {
                        if(psessionEntry->gLimHt20Params.protectionEnabled)
                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT;
                            limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                            limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                        }
                        else
                        {
                            pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                        }
                    }
                }
            }
            else
            {
                //Disable protection from 11G stations.
                psessionEntry->gLimHt20Params.protectionEnabled = false;

                //Change HT op mode appropriately.
                if(eSIR_HT_OP_MODE_NO_LEGACY_20MHZ_HT == pMac->lim.gHTOperMode)
                {
                    pMac->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
                    limEnableHtRifsProtection(pMac, false, overlap, pBeaconParams,psessionEntry);
                    limEnableHtOBSSProtection(pMac,  false, overlap, pBeaconParams,psessionEntry);
                }
            }
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from HT 20 Disabled"));)
            pBeaconParams->ht20MhzCoexist = psessionEntry->beaconParams.ht20Coexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_HT20MHZCOEXIST_CHANGED;
        }
        //for station role
        else
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from HT20 Disabled"));)
            pBeaconParams->ht20MhzCoexist = psessionEntry->beaconParams.ht20Coexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_HT20MHZCOEXIST_CHANGED;
        }
    }

    return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limEnableHTNonGfProtection
\brief based on cofig enables\disables protection from NonGf.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHTNonGfProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.

    //overlapping protection configuration check.
    if(overlap) {
    } else {
        //normal protection config check
        if (LIM_IS_AP_ROLE(psessionEntry) &&
            !psessionEntry->cfgProtection.nonGf) {
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL("protection from NonGf is disabled"));)
            return eSIR_SUCCESS;
        } else if(!LIM_IS_AP_ROLE(psessionEntry)) {
            //normal protection config check
            if (!pMac->lim.cfgProtection.nonGf) {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL("protection from NonGf is disabled"));)
                return eSIR_SUCCESS;
            }
        }
    }

    if (LIM_IS_AP_ROLE(psessionEntry)) {
        if ((enable) && (false == psessionEntry->beaconParams.llnNonGFCoexist))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Protection from non GF Enabled"));)
            pBeaconParams->llnNonGFCoexist = psessionEntry->beaconParams.llnNonGFCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }
        else if (!enable && (true == psessionEntry->beaconParams.llnNonGFCoexist))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from Non GF Disabled"));)
            pBeaconParams->llnNonGFCoexist = psessionEntry->beaconParams.llnNonGFCoexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }
    } else {
        if ((enable) && (false == psessionEntry->beaconParams.llnNonGFCoexist))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Protection from non GF Enabled"));)
            pBeaconParams->llnNonGFCoexist = psessionEntry->beaconParams.llnNonGFCoexist = true;
            pBeaconParams->paramChangeBitmap |= PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }
        else if (!enable && (true == psessionEntry->beaconParams.llnNonGFCoexist))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from Non GF Disabled"));)
            pBeaconParams->llnNonGFCoexist = psessionEntry->beaconParams.llnNonGFCoexist = false;
            pBeaconParams->paramChangeBitmap |= PARAM_NON_GF_DEVICES_PRESENT_CHANGED;
        }
    }

    return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limEnableHTLsigTxopProtection
\brief based on cofig enables\disables LsigTxop protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHTLsigTxopProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.

    //overlapping protection configuration check.
    if(overlap) {
    } else {
        //normal protection config check
        if (LIM_IS_AP_ROLE(psessionEntry) &&
           !psessionEntry->cfgProtection.lsigTxop) {
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL(" protection from LsigTxop not supported is disabled"));)
            return eSIR_SUCCESS;
        } else if(!LIM_IS_AP_ROLE(psessionEntry)) {
            //normal protection config check
            if(!pMac->lim.cfgProtection.lsigTxop) {
                // protection disabled.
                PELOG3(limLog(pMac, LOG3, FL(" protection from LsigTxop not supported is disabled"));)
                return eSIR_SUCCESS;
            }
        }
    }

    if (LIM_IS_AP_ROLE(psessionEntry)) {
        if ((enable) && (false == psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Protection from LsigTxop Enabled"));)
            pBeaconParams->fLsigTXOPProtectionFullSupport = psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = true;
            pBeaconParams->paramChangeBitmap |= PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }
        else if (!enable && (true == psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from LsigTxop Disabled"));)
            pBeaconParams->fLsigTXOPProtectionFullSupport= psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = false;
            pBeaconParams->paramChangeBitmap |= PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }
    } else {
        if ((enable) && (false == psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Protection from LsigTxop Enabled"));)
            pBeaconParams->fLsigTXOPProtectionFullSupport = psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = true;
            pBeaconParams->paramChangeBitmap |= PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }
        else if (!enable && (true == psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Protection from LsigTxop Disabled"));)
            pBeaconParams->fLsigTXOPProtectionFullSupport= psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = false;
            pBeaconParams->paramChangeBitmap |= PARAM_LSIG_TXOP_FULL_SUPPORT_CHANGED;
        }
    }
    return eSIR_SUCCESS;
}
//FIXME_PROTECTION : need to check for no APSD whenever we want to enable this protection.
//This check will be done at the caller.
/** -------------------------------------------------------------
\fn limEnableHtRifsProtection
\brief based on cofig enables\disables Rifs protection.
\param      tANI_U8 enable : 1=> enable protection, 0=> disable protection.
\param      tANI_U8 overlap: 1=> called from overlap context, 0 => called from assoc context.
\param      tpUpdateBeaconParams pBeaconParams
\return      None
  -------------------------------------------------------------*/
tSirRetStatus
limEnableHtRifsProtection(tpAniSirGlobal pMac, tANI_U8 enable,
    tANI_U8 overlap, tpUpdateBeaconParams pBeaconParams,tpPESession psessionEntry)
{
    if(!psessionEntry->htCapability)
        return eSIR_SUCCESS; // this protection  is only for HT stations.


    //overlapping protection configuration check.
    if(overlap) {
    } else {
         //normal protection config check
        if (LIM_IS_AP_ROLE(psessionEntry) &&
           !psessionEntry->cfgProtection.rifs) {
            // protection disabled.
            PELOG3(limLog(pMac, LOG3, FL(" protection from Rifs is disabled"));)
            return eSIR_SUCCESS;
        } else if (!LIM_IS_AP_ROLE(psessionEntry)) {
           //normal protection config check
           if(!pMac->lim.cfgProtection.rifs) {
              // protection disabled.
              PELOG3(limLog(pMac, LOG3, FL(" protection from Rifs is disabled"));)
              return eSIR_SUCCESS;
           }
        }
    }

    if (LIM_IS_AP_ROLE(psessionEntry)) {
        // Disabling the RIFS Protection means Enable the RIFS mode of operation in the BSS
        if ((!enable) && (false == psessionEntry->beaconParams.fRIFSMode))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Rifs protection Disabled"));)
            pBeaconParams->fRIFSMode = psessionEntry->beaconParams.fRIFSMode = true;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }
        // Enabling the RIFS Protection means Disable the RIFS mode of operation in the BSS
        else if (enable && (true == psessionEntry->beaconParams.fRIFSMode))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Rifs Protection Enabled"));)
            pBeaconParams->fRIFSMode = psessionEntry->beaconParams.fRIFSMode = false;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }
    }else
    {
        // Disabling the RIFS Protection means Enable the RIFS mode of operation in the BSS
        if ((!enable) && (false == psessionEntry->beaconParams.fRIFSMode))
        {
            PELOG1(limLog(pMac, LOG1, FL(" => Rifs protection Disabled"));)
            pBeaconParams->fRIFSMode = psessionEntry->beaconParams.fRIFSMode = true;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }
    // Enabling the RIFS Protection means Disable the RIFS mode of operation in the BSS
        else if (enable && (true == psessionEntry->beaconParams.fRIFSMode))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Rifs Protection Enabled"));)
            pBeaconParams->fRIFSMode = psessionEntry->beaconParams.fRIFSMode = false;
            pBeaconParams->paramChangeBitmap |= PARAM_RIFS_MODE_CHANGED;
        }
    }
    return eSIR_SUCCESS;
}

// ---------------------------------------------------------------------
/**
 * limEnableShortPreamble
 *
 * FUNCTION:
 * Enable/Disable short preamble
 *
 * LOGIC:
 *
 * ASSUMPTIONS:
 *
 * NOTE:
 *
 * @param enable        Flag to enable/disable short preamble
 * @return None
 */

tSirRetStatus
limEnableShortPreamble(tpAniSirGlobal pMac, tANI_U8 enable, tpUpdateBeaconParams pBeaconParams, tpPESession psessionEntry)
{
    tANI_U32 val;

    if (wlan_cfgGetInt(pMac, WNI_CFG_SHORT_PREAMBLE, &val) != eSIR_SUCCESS)
    {
        /* Could not get short preamble enabled flag from CFG. Log error. */
        limLog(pMac, LOGP, FL("could not retrieve short preamble flag"));
        return eSIR_FAILURE;
    }

    if (!val)
        return eSIR_SUCCESS;

    if (wlan_cfgGetInt(pMac, WNI_CFG_11G_SHORT_PREAMBLE_ENABLED, &val) != eSIR_SUCCESS)
    {
        limLog(pMac, LOGP, FL("could not retrieve 11G short preamble switching  enabled flag"));
        return eSIR_FAILURE;
    }

    if (!val)   // 11G short preamble switching is disabled.
        return eSIR_SUCCESS;

    if (LIM_IS_AP_ROLE(psessionEntry)) {
        if (enable && (psessionEntry->beaconParams.fShortPreamble == 0))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Short Preamble Enabled"));)
            psessionEntry->beaconParams.fShortPreamble = true;
            pBeaconParams->fShortPreamble = (tANI_U8) psessionEntry->beaconParams.fShortPreamble;
            pBeaconParams->paramChangeBitmap |= PARAM_SHORT_PREAMBLE_CHANGED;
        }
        else if (!enable && (psessionEntry->beaconParams.fShortPreamble == 1))
        {
            PELOG1(limLog(pMac, LOG1, FL("===> Short Preamble Disabled"));)
            psessionEntry->beaconParams.fShortPreamble = false;
            pBeaconParams->fShortPreamble = (tANI_U8) psessionEntry->beaconParams.fShortPreamble;
            pBeaconParams->paramChangeBitmap |= PARAM_SHORT_PREAMBLE_CHANGED;
        }
    }

    return eSIR_SUCCESS;
}

/**
 * limTxComplete
 *
 * Function:
 * This is LIM's very own "TX MGMT frame complete" completion routine.
 *
 * Logic:
 * LIM wants to send a MGMT frame (broadcast or unicast)
 * LIM allocates memory using palPktAlloc( ..., **pData, **pPacket )
 * LIM transmits the MGMT frame using the API:
 *  halTxFrame( ... pPacket, ..., (void *) limTxComplete, pData )
 * HDD, via halTxFrame/DXE, "transfers" the packet over to BMU
 * HDD, if it determines that a TX completion routine (in this case
 * limTxComplete) has been provided, will invoke this callback
 * LIM will try to free the TX MGMT packet that was earlier allocated, in order
 * to send this MGMT frame, using the PAL API palPktFree( ... pData, pPacket )
 *
 * Assumptions:
 * Presently, this is ONLY being used for MGMT frames/packets
 * TODO:
 * Would it do good for LIM to have some sort of "signature" validation to
 * ensure that the pData argument passed in was a buffer that was actually
 * allocated by LIM and/or is not corrupted?
 *
 * Note: FIXME and TODO
 * Looks like palPktFree() is interested in pPacket. But, when this completion
 * routine is called, only pData is made available to LIM!!
 *
 * @param void A pointer to pData. Shouldn't it be pPacket?!
 *
 * @return none
 */
void limTxComplete( tHalHandle hHal, void *pData, v_BOOL_t free)
{
  tpAniSirGlobal pMac;
  pMac = (tpAniSirGlobal)hHal;

#ifdef FIXME_PRIMA
  /* the trace logic needs to be fixed for Prima.  Refer to CR 306075 */
#ifdef TRACE_RECORD
    {
        tpSirMacMgmtHdr mHdr;
        v_U8_t         *pRxBd;
        vos_pkt_t      *pVosPkt;
        VOS_STATUS      vosStatus;



        pVosPkt = (vos_pkt_t *)pData;
        vosStatus = vos_pkt_peek_data( pVosPkt, 0, (v_PVOID_t *)&pRxBd, WLANHAL_RX_BD_HEADER_SIZE);

        if(VOS_IS_STATUS_SUCCESS(vosStatus))
        {
            mHdr = WDA_GET_RX_MAC_HEADER(pRxBd);

        }
    }
#endif
#endif

  if (free)
     palPktFree( pMac->hHdd,
                 HAL_TXRX_FRM_802_11_MGMT,
                 (void *) NULL, /* this is ignored and will likely be removed */
                 (void *) pData );  /* lim passed in pPacket in pData pointer */
}

/**
 * \brief This function updates lim global structure, if CB parameters in the BSS
 *  have changed, and sends an indication to HAL also with the
 * updated HT Parameters.
 * This function does not detect the change in the primary channel, that is done as part
 * of channel Switch IE processing.
 * If STA is configured with '20Mhz only' mode, then this function does not do anything
 * This function changes the CB mode, only if the self capability is set to '20 as well as 40Mhz'
 *
 *
 * \param pMac Pointer to global MAC structure
 *
 * \param pRcvdHTInfo Pointer to HT Info IE obtained from a  Beacon or
 * Probe Response
 *
 * \param bssIdx BSS Index of the Bss to which Station is associated.
 *
 *
 */

void limUpdateStaRunTimeHTSwitchChnlParams( tpAniSirGlobal   pMac,
                                  tDot11fIEHTInfo *pHTInfo,
                                  tANI_U8          bssIdx,
                                  tpPESession      psessionEntry)
{
    ePhyChanBondState secondaryChnlOffset = PHY_SINGLE_CHANNEL_CENTERED;
#if !defined WLAN_FEATURE_VOWIFI
    tANI_U32 localPwrConstraint;
#endif

   //If self capability is set to '20Mhz only', then do not change the CB mode.
   if( !limGetHTCapability( pMac, eHT_SUPPORTED_CHANNEL_WIDTH_SET, psessionEntry ))
        return;

   if ((RF_CHAN_14 >= psessionEntry->currentOperChannel) &&
       psessionEntry->force_24ghz_in_ht20) {
        limLog(pMac, LOG1,
               FL("force_24_gh_in_ht20 is set and channel is 2.4 Ghz"));
        return;
   }

#if !defined WLAN_FEATURE_VOWIFI
    if(wlan_cfgGetInt(pMac, WNI_CFG_LOCAL_POWER_CONSTRAINT, &localPwrConstraint) != eSIR_SUCCESS) {
        limLog( pMac, LOGP, FL( "Unable to get Local Power Constraint from cfg" ));
        return;
    }
#endif

    if (psessionEntry->ftPEContext.ftPreAuthSession) {
         limLog( pMac, LOGE, FL( "FT PREAUTH channel change is in progress"));
         return;
    }

    /*
     * Do not try to switch channel if RoC is in progress. RoC code path uses
     * pMac->lim.gpLimRemainOnChanReq to notify the upper layers that the device
     * has started listening on the channel requested as part of RoC, if we set
     * pMac->lim.gpLimRemainOnChanReq to NULL as we do below then the
     * upper layers will think that the channel change is not successful and the
     * RoC from the upper layer perspective will never end...
     */
    if (pMac->lim.gpLimRemainOnChanReq)
    {
        limLog(pMac, LOGE, FL( "RoC is in progress"));
        return;
    }

    if ( psessionEntry->htSecondaryChannelOffset != ( tANI_U8 ) pHTInfo->secondaryChannelOffset ||
         psessionEntry->htRecommendedTxWidthSet  != ( tANI_U8 ) pHTInfo->recommendedTxWidthSet )
    {
        psessionEntry->htSecondaryChannelOffset = ( ePhyChanBondState ) pHTInfo->secondaryChannelOffset;
        psessionEntry->htRecommendedTxWidthSet  = ( tANI_U8 ) pHTInfo->recommendedTxWidthSet;
        if ( eHT_CHANNEL_WIDTH_40MHZ == psessionEntry->htRecommendedTxWidthSet )
            secondaryChnlOffset = (ePhyChanBondState)pHTInfo->secondaryChannelOffset;

        // Notify HAL
        limLog( pMac, LOGW,  FL( "Channel Information in HT IE change"
                                 "d; sending notification to HAL." ) );
        limLog( pMac, LOGW,  FL( "Primary Channel: %d, Secondary Chan"
                                 "nel Offset: %d, Channel Width: %d" ),
                pHTInfo->primaryChannel, secondaryChnlOffset,
                psessionEntry->htRecommendedTxWidthSet );
        psessionEntry->channelChangeReasonCode=LIM_SWITCH_CHANNEL_OPERATION;
        pMac->lim.gpchangeChannelCallback = NULL;
        pMac->lim.gpchangeChannelData = NULL;

#if defined WLAN_FEATURE_VOWIFI
        limSendSwitchChnlParams( pMac, ( tANI_U8 ) pHTInfo->primaryChannel,
                                 secondaryChnlOffset, psessionEntry->maxTxPower,
                                 psessionEntry->peSessionId, VOS_TRUE);
#else
        limSendSwitchChnlParams( pMac, ( tANI_U8 ) pHTInfo->primaryChannel,
                                 secondaryChnlOffset,
                                 (tPowerdBm)localPwrConstraint,
                                 psessionEntry->peSessionId, VOS_TRUE);
#endif

        //In case of IBSS, if STA should update HT Info IE in its beacons.
       if (LIM_IS_IBSS_ROLE(psessionEntry)) {
            schSetFixedBeaconFields(pMac, psessionEntry);
       }
    }
} // End limUpdateStaRunTimeHTParams.

/**
 * \brief This function updates the lim global structure, if any of the
 * HT Capabilities have changed.
 *
 *
 * \param pMac Pointer to Global MAC structure
 *
 * \param pHTCapability Pointer to HT Capability Information Element
 * obtained from a Beacon or Probe Response
 *
 *
 *
 */

void limUpdateStaRunTimeHTCapability( tpAniSirGlobal   pMac,
                                      tDot11fIEHTCaps *pHTCaps )
{

    if ( pMac->lim.gHTLsigTXOPProtection != ( tANI_U8 ) pHTCaps->lsigTXOPProtection )
    {
        pMac->lim.gHTLsigTXOPProtection = ( tANI_U8 ) pHTCaps->lsigTXOPProtection;
       // Send change notification to HAL
    }

    if ( pMac->lim.gHTAMpduDensity != ( tANI_U8 ) pHTCaps->mpduDensity )
    {
       pMac->lim.gHTAMpduDensity = ( tANI_U8 ) pHTCaps->mpduDensity;
       // Send change notification to HAL
    }

    if ( pMac->lim.gHTMaxRxAMpduFactor != ( tANI_U8 ) pHTCaps->maxRxAMPDUFactor )
    {
       pMac->lim.gHTMaxRxAMpduFactor = ( tANI_U8 ) pHTCaps->maxRxAMPDUFactor;
       // Send change notification to HAL
    }


} // End limUpdateStaRunTimeHTCapability.

/**
 * \brief This function updates lim global structure, if any of the HT
 * Info Parameters have changed.
 *
 *
 * \param pMac Pointer to the global MAC structure
 *
 * \param pHTInfo Pointer to the HT Info IE obtained from a Beacon or
 * Probe Response
 *
 *
 */

void limUpdateStaRunTimeHTInfo( tpAniSirGlobal  pMac,
                                tDot11fIEHTInfo *pHTInfo, tpPESession psessionEntry)
{
    if ( psessionEntry->htRecommendedTxWidthSet != ( tANI_U8 )pHTInfo->recommendedTxWidthSet )
    {
        psessionEntry->htRecommendedTxWidthSet = ( tANI_U8 )pHTInfo->recommendedTxWidthSet;
        // Send change notification to HAL
    }

    if ( psessionEntry->beaconParams.fRIFSMode != ( tANI_U8 )pHTInfo->rifsMode )
    {
        psessionEntry->beaconParams.fRIFSMode = ( tANI_U8 )pHTInfo->rifsMode;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTServiceIntervalGranularity != ( tANI_U8 )pHTInfo->serviceIntervalGranularity )
    {
        pMac->lim.gHTServiceIntervalGranularity = ( tANI_U8 )pHTInfo->serviceIntervalGranularity;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTOperMode != ( tSirMacHTOperatingMode )pHTInfo->opMode )
    {
        pMac->lim.gHTOperMode = ( tSirMacHTOperatingMode )pHTInfo->opMode;
        // Send change notification to HAL
    }

    if ( psessionEntry->beaconParams.llnNonGFCoexist != pHTInfo->nonGFDevicesPresent )
    {
        psessionEntry->beaconParams.llnNonGFCoexist = ( tANI_U8 )pHTInfo->nonGFDevicesPresent;
    }

    if ( pMac->lim.gHTSTBCBasicMCS != ( tANI_U8 )pHTInfo->basicSTBCMCS )
    {
        pMac->lim.gHTSTBCBasicMCS = ( tANI_U8 )pHTInfo->basicSTBCMCS;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTDualCTSProtection != ( tANI_U8 )pHTInfo->dualCTSProtection )
    {
        pMac->lim.gHTDualCTSProtection = ( tANI_U8 )pHTInfo->dualCTSProtection;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTSecondaryBeacon != ( tANI_U8 )pHTInfo->secondaryBeacon )
    {
        pMac->lim.gHTSecondaryBeacon = ( tANI_U8 )pHTInfo->secondaryBeacon;
        // Send change notification to HAL
    }

    if ( psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport != ( tANI_U8 )pHTInfo->lsigTXOPProtectionFullSupport )
    {
        psessionEntry->beaconParams.fLsigTXOPProtectionFullSupport = ( tANI_U8 )pHTInfo->lsigTXOPProtectionFullSupport;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTPCOActive != ( tANI_U8 )pHTInfo->pcoActive )
    {
        pMac->lim.gHTPCOActive = ( tANI_U8 )pHTInfo->pcoActive;
        // Send change notification to HAL
    }

    if ( pMac->lim.gHTPCOPhase != ( tANI_U8 )pHTInfo->pcoPhase )
    {
        pMac->lim.gHTPCOPhase = ( tANI_U8 )pHTInfo->pcoPhase;
        // Send change notification to HAL
    }

} // End limUpdateStaRunTimeHTInfo.


/** -------------------------------------------------------------
\fn limProcessHalIndMessages
\brief callback function for HAL indication
\param   tpAniSirGlobal pMac
\param    tANI_U32 mesgId
\param    void *mesgParam
\return tSirRetStatu - status
  -------------------------------------------------------------*/

tSirRetStatus limProcessHalIndMessages(tpAniSirGlobal pMac, tANI_U32 msgId, void *msgParam )
{
  //its PE's responsibility to free msgparam when its done extracting the message parameters.
  tSirMsgQ msg;

  switch(msgId)
  {
    case SIR_LIM_DEL_TS_IND:
    case SIR_LIM_DELETE_STA_CONTEXT_IND:
    case SIR_LIM_BEACON_GEN_IND:
      msg.type = (tANI_U16) msgId;
      msg.bodyptr = msgParam;
      msg.bodyval = 0;
      break;

    default:
      vos_mem_free(msgParam);
      limLog(pMac, LOGP, FL("invalid message id = %d received"), msgId);
      return eSIR_FAILURE;
  }

  if (limPostMsgApi(pMac, &msg) != eSIR_SUCCESS)
  {
    vos_mem_free(msgParam);
    limLog(pMac, LOGP, FL("limPostMsgApi failed for msgid = %d"), msg.type);
    return eSIR_FAILURE;
  }
  return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limValidateDeltsReq
\brief Validates DelTs req originated by SME or by HAL and also sends halMsg_DelTs to HAL
\param   tpAniSirGlobal pMac
\param     tpSirDeltsReq pDeltsReq
\param   tSirMacAddr peerMacAddr
\return eSirRetStatus - status
  -------------------------------------------------------------*/

tSirRetStatus
limValidateDeltsReq(tpAniSirGlobal pMac, tpSirDeltsReq pDeltsReq, tSirMacAddr peerMacAddr,tpPESession psessionEntry)
{
    tpDphHashNode pSta;
    tANI_U8            tsStatus;
    tSirMacTSInfo *tsinfo;
    tANI_U32 i;
    tANI_U8 tspecIdx;
    /* if sta
     *  - verify assoc state
     *  - del tspec locally
     * if ap,
     *  - verify sta is in assoc state
     *  - del sta tspec locally
     */
    if(pDeltsReq == NULL)
    {
      PELOGE(limLog(pMac, LOGE, FL("Delete TS request pointer is NULL"));)
      return eSIR_FAILURE;
    }

    if (LIM_IS_STA_ROLE(psessionEntry) ||
        LIM_IS_BT_AMP_STA_ROLE(psessionEntry)) {
        tANI_U32 val;

        // station always talks to the AP
        pSta = dphGetHashEntry(pMac, DPH_STA_HASH_INDEX_PEER, &psessionEntry->dph.dphHashTable);

        val = sizeof(tSirMacAddr);
       sirCopyMacAddr(peerMacAddr,psessionEntry->bssId);

    } else {
        tANI_U16 assocId;
        tANI_U8 *macaddr = (tANI_U8 *) peerMacAddr;

        assocId = pDeltsReq->aid;
        if (assocId != 0)
            pSta = dphGetHashEntry(pMac, assocId, &psessionEntry->dph.dphHashTable);
        else
            pSta = dphLookupHashEntry(pMac, pDeltsReq->macAddr, &assocId, &psessionEntry->dph.dphHashTable);

        if (pSta != NULL)
            // TBD: check sta assoc state as well
            for (i =0; i < sizeof(tSirMacAddr); i++)
                macaddr[i] = pSta->staAddr[i];
    }

    if (pSta == NULL)
    {
        PELOGE(limLog(pMac, LOGE, "Cannot find station context for delts req");)
        return eSIR_FAILURE;
    }

    if ((! pSta->valid) ||
        (pSta->mlmStaContext.mlmState != eLIM_MLM_LINK_ESTABLISHED_STATE))
    {
        PELOGE(limLog(pMac, LOGE, "Invalid Sta (or state) for DelTsReq");)
        return eSIR_FAILURE;
    }

    pDeltsReq->req.wsmTspecPresent = 0;
    pDeltsReq->req.wmeTspecPresent = 0;
    pDeltsReq->req.lleTspecPresent = 0;

    if ((pSta->wsmEnabled) &&
        (pDeltsReq->req.tspec.tsinfo.traffic.accessPolicy != SIR_MAC_ACCESSPOLICY_EDCA))
        pDeltsReq->req.wsmTspecPresent = 1;
    else if (pSta->wmeEnabled)
        pDeltsReq->req.wmeTspecPresent = 1;
    else if (pSta->lleEnabled)
        pDeltsReq->req.lleTspecPresent = 1;
    else
    {
        PELOGW(limLog(pMac, LOGW, FL("DELTS_REQ ignore - qos is disabled"));)
        return eSIR_FAILURE;
    }

    tsinfo = pDeltsReq->req.wmeTspecPresent ? &pDeltsReq->req.tspec.tsinfo
                                            : &pDeltsReq->req.tsinfo;
   PELOG1(limLog(pMac, LOG1,
           FL("received DELTS_REQ message (wmeTspecPresent = %d, lleTspecPresent = %d, wsmTspecPresent = %d, tsid %d,  up %d, direction = %d)"),
           pDeltsReq->req.wmeTspecPresent, pDeltsReq->req.lleTspecPresent, pDeltsReq->req.wsmTspecPresent,
           tsinfo->traffic.tsid, tsinfo->traffic.userPrio, tsinfo->traffic.direction);)

       // if no Access Control, ignore the request

    if (limAdmitControlDeleteTS(pMac, pSta->assocId, tsinfo, &tsStatus, &tspecIdx)
        != eSIR_SUCCESS)
    {
       PELOGE(limLog(pMac, LOGE, "ERROR DELTS request for sta assocId %d (tsid %d, up %d)",
               pSta->assocId, tsinfo->traffic.tsid, tsinfo->traffic.userPrio);)
        return eSIR_FAILURE;
    }
    else if ((tsinfo->traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_HCCA) ||
             (tsinfo->traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_BOTH))
    {
      //edca only now.
    }
    else
    {
      if(tsinfo->traffic.accessPolicy == SIR_MAC_ACCESSPOLICY_EDCA)
      {
        //send message to HAL to delete TS
        if(eSIR_SUCCESS != limSendHalMsgDelTs(pMac,
                                              pSta->staIndex,
                                              tspecIdx,
                                              pDeltsReq->req,
                                              psessionEntry->peSessionId,
                                              psessionEntry->bssId))
        {
          limLog(pMac, LOGW, FL("DelTs with UP %d failed in limSendHalMsgDelTs - ignoring request"),
                           tsinfo->traffic.userPrio);
           return eSIR_FAILURE;
        }
      }
    }
    return eSIR_SUCCESS;
}

/** -------------------------------------------------------------
\fn limRegisterHalIndCallBack
\brief registers callback function to HAL for any indication.
\param   tpAniSirGlobal pMac
\return none.
  -------------------------------------------------------------*/
void
limRegisterHalIndCallBack(tpAniSirGlobal pMac)
{
    tSirMsgQ msg;
    tpHalIndCB pHalCB;

    pHalCB = vos_mem_malloc(sizeof(tHalIndCB));
    if ( NULL == pHalCB )
    {
       limLog(pMac, LOGP, FL("AllocateMemory() failed"));
       return;
    }

    pHalCB->pHalIndCB = limProcessHalIndMessages;

    msg.type = WDA_REGISTER_PE_CALLBACK;
    msg.bodyptr = pHalCB;
    msg.bodyval = 0;

    MTRACE(macTraceMsgTx(pMac, NO_SESSION, msg.type));
    if(eSIR_SUCCESS != wdaPostCtrlMsg(pMac, &msg))
    {
        vos_mem_free(pHalCB);
        limLog(pMac, LOGP, FL("wdaPostCtrlMsg() failed"));
    }

    return;
}

/** -------------------------------------------------------------
\fn limProcessDelTsInd
\brief Handles the DeleteTS indication coming from HAL or generated by
       PE itself in some error cases. Validates the request, sends the
       DelTs action frame to the Peer and sends DelTs indication to HDD.
\param   tpAniSirGlobal pMac
\param  tSirMsgQ limMsg
\return None
-------------------------------------------------------------*/
void
limProcessDelTsInd(tpAniSirGlobal pMac, tpSirMsgQ limMsg)
{
  tpDphHashNode         pSta;
  tpDelTsParams         pDelTsParam = (tpDelTsParams) (limMsg->bodyptr);
  tpSirDeltsReq         pDelTsReq = NULL;
  tSirMacAddr           peerMacAddr;
  tpSirDeltsReqInfo     pDelTsReqInfo;
  tpLimTspecInfo        pTspecInfo;
  tpPESession           psessionEntry;
  tANI_U8               sessionId;

if((psessionEntry = peFindSessionByBssid(pMac,pDelTsParam->bssId,&sessionId))== NULL)
    {
         limLog(pMac, LOGE,FL("session does not exist for given BssId"));
         vos_mem_free(limMsg->bodyptr);
         limMsg->bodyptr = NULL;
         return;
    }

  pTspecInfo = &(pMac->lim.tspecInfo[pDelTsParam->tspecIdx]);
  if(pTspecInfo->inuse == false)
  {
    PELOGE(limLog(pMac, LOGE, FL("tspec entry with index %d is not in use"), pDelTsParam->tspecIdx);)
    goto error1;
  }

  pSta = dphGetHashEntry(pMac, pTspecInfo->assocId, &psessionEntry->dph.dphHashTable);
  if(pSta == NULL)
  {
    limLog(pMac, LOGE, FL("Could not find entry in DPH table for assocId = %d"),
                pTspecInfo->assocId);
    goto error1;
  }

  pDelTsReq = vos_mem_malloc(sizeof(tSirDeltsReq));
  if ( NULL == pDelTsReq )
  {
     PELOGE(limLog(pMac, LOGE, FL("AllocateMemory() failed"));)
     goto error1;
  }

  vos_mem_set( (tANI_U8 *)pDelTsReq, sizeof(tSirDeltsReq), 0);

  if(pSta->wmeEnabled)
    vos_mem_copy( &(pDelTsReq->req.tspec), &(pTspecInfo->tspec), sizeof(tSirMacTspecIE));
  else
    vos_mem_copy( &(pDelTsReq->req.tsinfo), &(pTspecInfo->tspec.tsinfo), sizeof(tSirMacTSInfo));


  //validate the req
  if (eSIR_SUCCESS != limValidateDeltsReq(pMac, pDelTsReq, peerMacAddr,psessionEntry))
  {
    PELOGE(limLog(pMac, LOGE, FL("limValidateDeltsReq failed"));)
    goto error2;
  }
  PELOG1(limLog(pMac, LOG1, "Sent DELTS request to station with "
                "assocId = %d MacAddr = "MAC_ADDRESS_STR,
                pDelTsReq->aid, MAC_ADDR_ARRAY(peerMacAddr));)

  limSendDeltsReqActionFrame(pMac, peerMacAddr, pDelTsReq->req.wmeTspecPresent, &pDelTsReq->req.tsinfo, &pDelTsReq->req.tspec,
          psessionEntry);

  // prepare and send an sme indication to HDD
  pDelTsReqInfo = vos_mem_malloc(sizeof(tSirDeltsReqInfo));
  if ( NULL == pDelTsReqInfo )
  {
     PELOGE(limLog(pMac, LOGE, FL("AllocateMemory() failed"));)
     goto error3;
  }
  vos_mem_set( (tANI_U8 *)pDelTsReqInfo, sizeof(tSirDeltsReqInfo), 0);

  if(pSta->wmeEnabled)
    vos_mem_copy( &(pDelTsReqInfo->tspec), &(pTspecInfo->tspec), sizeof(tSirMacTspecIE));
  else
    vos_mem_copy( &(pDelTsReqInfo->tsinfo), &(pTspecInfo->tspec.tsinfo), sizeof(tSirMacTSInfo));

  limSendSmeDeltsInd(pMac, pDelTsReqInfo, pDelTsReq->aid,psessionEntry);

error3:
  vos_mem_free(pDelTsReqInfo);
error2:
  vos_mem_free(pDelTsReq);
error1:
  vos_mem_free(limMsg->bodyptr);
  limMsg->bodyptr = NULL;
  return;
}

/**
 * @function :  limPostSMStateUpdate()
 *
 * @brief  :  This function Updates the HAL and Softmac about the change in the STA's SMPS state.
 *
 *      LOGIC:
 *
 *      ASSUMPTIONS:
 *          NA
 *
 *      NOTE:
 *          NA
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  limMsg - Lim Message structure object with the MimoPSparam in body
 * @return None
 */
tSirRetStatus 
limPostSMStateUpdate(tpAniSirGlobal pMac,
        tANI_U16 staIdx, tSirMacHTMIMOPowerSaveState state,
        tANI_U8 *pPeerStaMac, tANI_U8 sessionId)
{
    tSirRetStatus             retCode = eSIR_SUCCESS;
    tSirMsgQ                    msgQ;
    tpSetMIMOPS            pMIMO_PSParams;

    msgQ.reserved = 0;
    msgQ.type = WDA_SET_MIMOPS_REQ;

    // Allocate for WDA_SET_MIMOPS_REQ
    pMIMO_PSParams = vos_mem_malloc(sizeof(tSetMIMOPS));
    if ( NULL == pMIMO_PSParams )
    {
        limLog( pMac, LOGP,FL(" AllocateMemory failed"));
        return eSIR_MEM_ALLOC_FAILED;
    }

    pMIMO_PSParams->htMIMOPSState = state;
    pMIMO_PSParams->staIdx = staIdx;
    pMIMO_PSParams->fsendRsp = true;
    pMIMO_PSParams->sessionId = sessionId;
    vos_mem_copy(pMIMO_PSParams->peerMac, pPeerStaMac,
                    sizeof( tSirMacAddr ));

    msgQ.bodyptr = pMIMO_PSParams;
    msgQ.bodyval = 0;

    limLog( pMac, LOG2, FL( "Sending WDA_SET_MIMOPS_REQ..." ));

    MTRACE(macTraceMsgTx(pMac, NO_SESSION, msgQ.type));
    retCode = wdaPostCtrlMsg( pMac, &msgQ );
    if (eSIR_SUCCESS != retCode)
    {
        limLog( pMac, LOGP, FL("Posting WDA_SET_MIMOPS_REQ to HAL failed! Reason = %d"), retCode );
        vos_mem_free(pMIMO_PSParams);
        return retCode;
    }

    return retCode;
}

void limPktFree (
    tpAniSirGlobal  pMac,
    eFrameType      frmType,
    tANI_U8         *pRxPacketInfo,
    void            *pBody)
{
    (void) pMac; (void) frmType; (void) pRxPacketInfo; (void) pBody;
}

/**
 * limGetBDfromRxPacket()
 *
 *FUNCTION:
 * This function is called to get pointer to
 * Buffer Descriptor containing MAC header & other control
 * info from the body of the message posted to LIM.
 *
 *LOGIC:
 * NA
 *
 *ASSUMPTIONS:
 * NA
 *
 *NOTE:
 * NA
 *
 * @param  body    - Received message body
 * @param  pRxPacketInfo     - Pointer to received BD
 * @return None
 */

void
limGetBDfromRxPacket(tpAniSirGlobal pMac, void *body, tANI_U32 **pRxPacketInfo)
{
    *pRxPacketInfo = (tANI_U32 *) body;
} /*** end limGetBDfromRxPacket() ***/





void limRessetScanChannelInfo(tpAniSirGlobal pMac)
{
    vos_mem_set(&pMac->lim.scanChnInfo, sizeof(tLimScanChnInfo), 0);
}


void limAddScanChannelInfo(tpAniSirGlobal pMac, tANI_U8 channelId)
{
    tANI_U8 i;
    tANI_BOOLEAN fFound = eANI_BOOLEAN_FALSE;

    for(i = 0; i < pMac->lim.scanChnInfo.numChnInfo; i++)
    {
        if(pMac->lim.scanChnInfo.scanChn[i].channelId == channelId)
        {
            pMac->lim.scanChnInfo.scanChn[i].numTimeScan++;
            fFound = eANI_BOOLEAN_TRUE;
            break;
        }
    }
    if(eANI_BOOLEAN_FALSE == fFound)
    {
        if(pMac->lim.scanChnInfo.numChnInfo < SIR_MAX_SUPPORTED_CHANNEL_LIST)
        {
            pMac->lim.scanChnInfo.scanChn[pMac->lim.scanChnInfo.numChnInfo].channelId = channelId;
            pMac->lim.scanChnInfo.scanChn[pMac->lim.scanChnInfo.numChnInfo++].numTimeScan = 1;
        }
        else
        {
            PELOGW(limLog(pMac, LOGW, FL(" -- number of channels exceed mac"));)
        }
    }
}

/**
 * lim_add_channel_status_info() - store
 * 	chan status info into Global MAC structure
 * @p_mac: Pointer to Global MAC structure
 * @channel_stat: Pointer to chan status info reported by firmware
 * @channel_id: current channel id
 *
 * Return: None
 */
void lim_add_channel_status_info(tpAniSirGlobal p_mac,
	struct lim_channel_status *channel_stat, uint8_t channel_id)
{
	uint8_t i;
	boolean found = false;
	struct lim_scan_channel_status *channel_info =
		&p_mac->lim.scan_channel_status;
	struct lim_channel_status *channel_status_list =
		channel_info->channel_status_list;
	uint8_t total_channel = channel_info->total_channel;

	if (ACS_FW_REPORT_PARAM_CONFIGURED) {
	    for (i = 0; i < total_channel; i++) {
		if (channel_status_list[i].channel_id == channel_id) {
		    if (channel_stat->cmd_flags ==
			    WMI_CHAN_INFO_END_RESP &&
			    channel_status_list[i].cmd_flags ==
			    WMI_CHAN_INFO_START_RESP) {
			/* adjust to delta value for counts */
			channel_stat->rx_clear_count -=
			    channel_status_list[i].rx_clear_count;
			channel_stat->cycle_count -=
			    channel_status_list[i].cycle_count;
			channel_stat->rx_frame_count -=
			    channel_status_list[i].rx_frame_count;
			channel_stat->tx_frame_count -=
			    channel_status_list[i].tx_frame_count;
			channel_stat->bss_rx_cycle_count -=
			    channel_status_list[i].bss_rx_cycle_count;
		    }
		    vos_mem_copy(
			    &channel_status_list[i],
			    channel_stat,
			    sizeof(*channel_status_list));
		    found = true;
		    break;
		}
	    }
	    if (!found) {
		if (total_channel <
			SIR_MAX_SUPPORTED_ACS_CHANNEL_LIST) {
		    vos_mem_copy(
			    &channel_status_list[total_channel++],
			    channel_stat,
			    sizeof(*channel_status_list));
		    channel_info->total_channel = total_channel;
		} else {
		    PELOGW(limLog(p_mac, LOGW,
				FL("Chan cnt exceed, channel_id=%d"),
				channel_id);)
		}
	    }
	}
	return;
}


/**
 * @function :  limIsChannelValidForChannelSwitch()
 *
 * @brief  :  This function checks if the channel to which AP
 *            is expecting us to switch, is a valid channel for us.
 *      LOGIC:
 *
 *      ASSUMPTIONS:
 *          NA
 *
 *      NOTE:
 *          NA
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  channel - New channel to which we are expected to move
 * @return None
 */
tAniBool
limIsChannelValidForChannelSwitch(tpAniSirGlobal pMac, tANI_U8 channel)
{
    tANI_U8  index;
    tANI_U32    validChannelListLen = WNI_CFG_VALID_CHANNEL_LIST_LEN;
    tSirMacChanNum   validChannelList[WNI_CFG_VALID_CHANNEL_LIST_LEN];
#ifdef FEATURE_WLAN_DISABLE_CHANNEL_SWITCH
    if (!vos_is_chan_ok_for_dnbs(channel)) {
        PELOGE(limLog(pMac, LOGE, FL("channel is not valid for dnbs"));)
        return (eSIR_FALSE);
    }
#endif
    if (wlan_cfgGetStr(pMac, WNI_CFG_VALID_CHANNEL_LIST,
          (tANI_U8 *)validChannelList,
          (tANI_U32 *)&validChannelListLen) != eSIR_SUCCESS)
    {
        PELOGE(limLog(pMac, LOGE, FL("could not retrieve valid channel list"));)
        return (eSIR_FALSE);
    }

    for(index = 0; index < validChannelListLen; index++)
    {
        if(validChannelList[index] == channel)
            return (eSIR_TRUE);
    }

    /* channel does not belong to list of valid channels */
    return (eSIR_FALSE);
}

/**------------------------------------------------------
\fn     __limFillTxControlParams
\brief  Fill the message for stopping/resuming tx.

\param  pMac
\param  pTxCtrlMsg - Pointer to tx control message.
\param  type - Which way we want to stop/ resume tx.
\param  mode - To stop/resume.
 -------------------------------------------------------*/
static eHalStatus
__limFillTxControlParams(tpAniSirGlobal pMac, tpTxControlParams  pTxCtrlMsg,
                                        tLimQuietTxMode type, tLimControlTx mode)
{
    tpPESession psessionEntry = &pMac->lim.gpSession[0];

    if (mode == eLIM_STOP_TX)
        pTxCtrlMsg->stopTx =  eANI_BOOLEAN_TRUE;
    else
        pTxCtrlMsg->stopTx =  eANI_BOOLEAN_FALSE;

    switch (type)
    {
        case eLIM_TX_ALL:
            /** Stops/resumes transmission completely */
            pTxCtrlMsg->fCtrlGlobal = 1;
            break;

        case eLIM_TX_BSS_BUT_BEACON:
            /** Stops/resumes transmission on a particular BSS. Stopping BSS, doesnt
              *  stop beacon transmission.
              */
            pTxCtrlMsg->ctrlBss = 1;
            pTxCtrlMsg->bssBitmap    |= (1 << psessionEntry->bssIdx);
            break;

        case eLIM_TX_STA:
            /** Memory for station bitmap is allocated dynamically in caller of this
              *  so decode properly here and fill the bitmap. Now not implemented,
              *  fall through.
              */
        case eLIM_TX_BSS:
            //Fall thru...
        default:
            PELOGW(limLog(pMac, LOGW, FL("Invalid case: Not Handled"));)
            return eHAL_STATUS_FAILURE;
    }

    return eHAL_STATUS_SUCCESS;
}

/**
 * @function :  limFrameTransmissionControl()
 *
 * @brief  :  This API is called by the user to halt/resume any frame
 *       transmission from the device. If stopped, all frames will be
 *            queued starting from hardware. Then back-pressure
 *            is built till the driver.
 *      LOGIC:
 *
 *      ASSUMPTIONS:
 *          NA
 *
 *      NOTE:
 *          NA
 *
 * @param  pMac - Pointer to Global MAC structure
 * @return None
 */
void limFrameTransmissionControl(tpAniSirGlobal pMac, tLimQuietTxMode type, tLimControlTx mode)
{

    eHalStatus status = eHAL_STATUS_FAILURE;
    tpTxControlParams pTxCtrlMsg;
    tSirMsgQ          msgQ;
    tANI_U8           nBytes = 0;  // No of bytes required for station bitmap.

    /** Allocate only required number of bytes for station bitmap
     * Make it to align to 4 byte boundary  */
    nBytes = (tANI_U8)HALMSG_NUMBYTES_STATION_BITMAP(pMac->lim.maxStation);

    pTxCtrlMsg = vos_mem_malloc(sizeof(*pTxCtrlMsg) + nBytes);
    if ( NULL == pTxCtrlMsg )
    {
        limLog(pMac, LOGP, FL("AllocateMemory() failed"));
        return;
    }

    vos_mem_set((void *) pTxCtrlMsg,
               (sizeof(*pTxCtrlMsg) + nBytes), 0);
    status = __limFillTxControlParams(pMac, pTxCtrlMsg, type, mode);
    if (status != eHAL_STATUS_SUCCESS)
    {
        vos_mem_free(pTxCtrlMsg);
        limLog(pMac, LOGP, FL("__limFillTxControlParams failed, status = %d"), status);
        return;
    }

    msgQ.bodyptr = (void *) pTxCtrlMsg;
    msgQ.bodyval = 0;
    msgQ.reserved = 0;
    msgQ.type = WDA_TRANSMISSION_CONTROL_IND;

    MTRACE(macTraceMsgTx(pMac, NO_SESSION, msgQ.type));
    if(wdaPostCtrlMsg( pMac, &msgQ) != eSIR_SUCCESS)
    {
        vos_mem_free(pTxCtrlMsg);
        limLog( pMac, LOGP, FL("Posting Message to HAL failed"));
        return;
    }

    if (mode == eLIM_STOP_TX)
        {
            PELOG1(limLog(pMac, LOG1, FL("Stopping the transmission of all packets, indicated softmac"));)
        }
    else
        {
            PELOG1(limLog(pMac, LOG1, FL("Resuming the transmission of all packets, indicated softmac"));)
        }
    return;
}


/**
 * @function :  limRestorePreChannelSwitchState()
 *
 * @brief  :  This API is called by the user to undo any
 *            specific changes done on the device during
 *            channel switch.
 *      LOGIC:
 *
 *      ASSUMPTIONS:
 *          NA
 *
 *      NOTE:
 *          NA
 *
 * @param  pMac - Pointer to Global MAC structure
 * @return None
 */

tSirRetStatus
limRestorePreChannelSwitchState(tpAniSirGlobal pMac, tpPESession psessionEntry)
{

    tSirRetStatus retCode = eSIR_SUCCESS;
    tANI_U32      val = 0;

    if (!LIM_IS_STA_ROLE(psessionEntry))
        return retCode;

    /* Channel switch should be ready for the next time */
    psessionEntry->gLimSpecMgmt.dot11hChanSwState = eLIM_11H_CHANSW_INIT;

    /* Restore the frame transmission, all the time. */
    limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_RESUME_TX);

    /* Free to enter BMPS */
    limSendSmePostChannelSwitchInd(pMac);

    //Background scan is now enabled by SME
    if(pMac->lim.gLimBackgroundScanTerminate == FALSE)
    {
        /* Enable background scan if already enabled, else don't bother */
        if ((retCode = wlan_cfgGetInt(pMac, WNI_CFG_BACKGROUND_SCAN_PERIOD,
                      &val)) != eSIR_SUCCESS)

        {
            limLog(pMac, LOGP, FL("could not retrieve Background scan period value"));
            return (retCode);
        }

        if (val > 0 && TX_TIMER_VALID(pMac->lim.limTimers.gLimBackgroundScanTimer))
        {
            MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE,
                     psessionEntry->peSessionId, eLIM_BACKGROUND_SCAN_TIMER));
            if(tx_timer_activate(&pMac->lim.limTimers.gLimBackgroundScanTimer) != TX_SUCCESS)
            {
                limLog(pMac, LOGP, FL("Could not restart background scan timer, doing LOGP"));
                return (eSIR_FAILURE);
            }

        }
    }

    /* Enable heartbeat timer */
    if (TX_TIMER_VALID(pMac->lim.limTimers.gLimHeartBeatTimer))
    {
        MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE,
                 psessionEntry->peSessionId, eLIM_HEART_BEAT_TIMER));
        if((limActivateHearBeatTimer(pMac, psessionEntry) != TX_SUCCESS) &&
              (!IS_ACTIVEMODE_OFFLOAD_FEATURE_ENABLE))
        {
            limLog(pMac, LOGP, FL("Could not restart heartbeat timer, doing LOGP"));
            return (eSIR_FAILURE);
        }
    }
    return (retCode);
}


/**--------------------------------------------
\fn       limRestorePreQuietState
\brief   Restore the pre quiet state

\param pMac
\return NONE
---------------------------------------------*/
tSirRetStatus limRestorePreQuietState(tpAniSirGlobal pMac, tpPESession psessionEntry)
{

    tSirRetStatus retCode = eSIR_SUCCESS;
    tANI_U32      val = 0;

    if (pMac->lim.gLimSystemRole != eLIM_STA_ROLE)
             return retCode;

    /* Quiet should be ready for the next time */
    psessionEntry->gLimSpecMgmt.quietState = eLIM_QUIET_INIT;

    /* Restore the frame transmission, all the time. */
    if (psessionEntry->gLimSpecMgmt.quietState == eLIM_QUIET_RUNNING)
        limFrameTransmissionControl(pMac, eLIM_TX_ALL, eLIM_RESUME_TX);


    //Background scan is now enabled by SME
    if(pMac->lim.gLimBackgroundScanTerminate == FALSE)
    {
        /* Enable background scan if already enabled, else don't bother */
        if ((retCode = wlan_cfgGetInt(pMac, WNI_CFG_BACKGROUND_SCAN_PERIOD,
                      &val)) != eSIR_SUCCESS)

        {
            limLog(pMac, LOGP, FL("could not retrieve Background scan period value"));
            return (retCode);
        }

        if (val > 0 && TX_TIMER_VALID(pMac->lim.limTimers.gLimBackgroundScanTimer))
        {
            MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE, psessionEntry->peSessionId, eLIM_BACKGROUND_SCAN_TIMER));
            if(tx_timer_activate(&pMac->lim.limTimers.gLimBackgroundScanTimer) != TX_SUCCESS)
            {
                limLog(pMac, LOGP, FL("Could not restart background scan timer, doing LOGP"));
                return (eSIR_FAILURE);
            }

        }
    }

    /* Enable heartbeat timer */
    if (TX_TIMER_VALID(pMac->lim.limTimers.gLimHeartBeatTimer))
    {
        MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE, psessionEntry->peSessionId, eLIM_HEART_BEAT_TIMER));
        if(limActivateHearBeatTimer(pMac, psessionEntry) != TX_SUCCESS)
        {
            limLog(pMac, LOGP, FL("Could not restart heartbeat timer, doing LOGP"));
            return (eSIR_FAILURE);
        }
    }
    return (retCode);
}


/**
 * @function: limPrepareFor11hChannelSwitch()
 *
 * @brief  :  This API is called by the user to prepare for
 *            11h channel switch. As of now, the API does
 *            very minimal work. User can add more into the
 *            same API if needed.
 *      LOGIC:
 *
 *      ASSUMPTIONS:
 *          NA
 *
 *      NOTE:
 *          NA
 *
 * @param  pMac - Pointer to Global MAC structure
 * @param  psessionEntry
 * @return None
 */
void
limPrepareFor11hChannelSwitch(tpAniSirGlobal pMac, tpPESession psessionEntry)
{
    if (!LIM_IS_STA_ROLE(psessionEntry))
        return;

    /* Flag to indicate 11h channel switch in progress */
    psessionEntry->gLimSpecMgmt.dot11hChanSwState = eLIM_11H_CHANSW_RUNNING;

    /* Disable, Stop background scan if enabled and running */
    limDeactivateAndChangeTimer(pMac, eLIM_BACKGROUND_SCAN_TIMER);

    /* Stop heart-beat timer to stop heartbeat disassociation */
    limHeartBeatDeactivateAndChangeTimer(pMac, psessionEntry);

    if(pMac->lim.gLimSmeState == eLIM_SME_LINK_EST_WT_SCAN_STATE ||
        pMac->lim.gLimSmeState == eLIM_SME_CHANNEL_SCAN_STATE)
    {
        PELOGE(limLog(pMac, LOG1, FL("Posting finish scan as we are in scan state"));)
        /* Stop ongoing scanning if any */
        if (GET_LIM_PROCESS_DEFD_MESGS(pMac))
        {
            //Set the resume channel to Any valid channel (invalid).
            //This will instruct HAL to set it to any previous valid channel.
            peSetResumeChannel(pMac, 0, 0);
            limSendHalFinishScanReq(pMac, eLIM_HAL_FINISH_SCAN_WAIT_STATE);
        }
        else
        {
            limRestorePreChannelSwitchState(pMac, psessionEntry);
        }
        return;
    }
    else
    {
        PELOGE(limLog(pMac, LOG1, FL("Not in scan state, start channel switch timer"));)
        /** We are safe to switch channel at this point */
        limStopTxAndSwitchChannel(pMac, psessionEntry->peSessionId);
    }
}



/**----------------------------------------------------
\fn        limGetNwType

\brief    Get type of the network from data packet or beacon
\param pMac
\param channelNum - Channel number
\param type - Type of packet.
\param pBeacon - Pointer to beacon or probe response

\return Network type a/b/g.
-----------------------------------------------------*/
tSirNwType limGetNwType(tpAniSirGlobal pMac, tANI_U8 channelNum, tANI_U32 type, tpSchBeaconStruct pBeacon)
{
    tSirNwType nwType = eSIR_11B_NW_TYPE;

    if (type == SIR_MAC_DATA_FRAME)
    {
        if ((channelNum > 0) && (channelNum < 15))
        {
            nwType = eSIR_11G_NW_TYPE;
        }
        else
        {
            nwType = eSIR_11A_NW_TYPE;
        }
    }
    else
    {
        if ((channelNum > 0) && (channelNum < 15))
        {
            int i;
            /*
             * 11b or 11g packet
             * 11g if extended Rate IE is present or
             * if there is an A rate in suppRate IE
             */
            for (i = 0; i < pBeacon->supportedRates.numRates; i++)
            {
                if (sirIsArate(pBeacon->supportedRates.rate[i] & 0x7f))
                {
                    nwType = eSIR_11G_NW_TYPE;
                    break;
                }
            }
            if (pBeacon->extendedRatesPresent)
            {
                PELOG3(limLog(pMac, LOG3, FL("Beacon, nwtype=G"));)
                nwType = eSIR_11G_NW_TYPE;
            }
        }
        else
        {
            // 11a packet
            PELOG3(limLog(pMac, LOG3,FL("Beacon, nwtype=A"));)
            nwType = eSIR_11A_NW_TYPE;
        }
    }
    return nwType;
}


/**---------------------------------------------------------
\fn        limGetChannelFromBeacon
\brief    To extract channel number from beacon

\param pMac
\param pBeacon - Pointer to beacon or probe rsp
\return channel number
-----------------------------------------------------------*/
tANI_U8 limGetChannelFromBeacon(tpAniSirGlobal pMac, tpSchBeaconStruct pBeacon)
{
    tANI_U8 channelNum = 0;

    if (pBeacon->dsParamsPresent)
        channelNum = pBeacon->channelNumber;
    else if(pBeacon->HTInfo.present)
        channelNum = pBeacon->HTInfo.primaryChannel;
    else
        channelNum = pBeacon->channelNumber;

    return channelNum;
}


/** ---------------------------------------------------------
\fn      limSetTspecUapsdMask
\brief   This function sets the PE global variable:
\        1) gUapsdPerAcTriggerEnableMask and
\        2) gUapsdPerAcDeliveryEnableMask
\        based on the user priority field and direction field
\        in the TS Info Fields.
\
\        An AC is a trigger-enabled AC if the PSB subfield
\        is set to 1 in the up link direction.
\        An AC is a delivery-enabled AC if the PSB subfield
\        is set to 1 in the down-link direction.
\
\param   tpAniSirGlobal  pMac
\param   tSirMacTSInfo   pTsInfo
\param   tANI_U32        action
\return  None
 ------------------------------------------------------------*/
void limSetTspecUapsdMask(tpAniSirGlobal pMac, tSirMacTSInfo *pTsInfo, tANI_U32 action)
{
    tANI_U8   userPrio = (tANI_U8)pTsInfo->traffic.userPrio;
    tANI_U16  direction = pTsInfo->traffic.direction;
    tANI_U8   ac = upToAc(userPrio);

    PELOG1(limLog(pMac, LOG1, FL(" Set UAPSD mask for AC %d, direction %d, action=%d (1=set,0=clear) "),ac, direction, action );)

    /* Converting AC to appropriate Uapsd Bit Mask
     * AC_BE(0) --> UAPSD_BITOFFSET_ACVO(3)
     * AC_BK(1) --> UAPSD_BITOFFSET_ACVO(2)
     * AC_VI(2) --> UAPSD_BITOFFSET_ACVO(1)
     * AC_VO(3) --> UAPSD_BITOFFSET_ACVO(0)
     */
    ac = ((~ac) & 0x3);

    if (action == CLEAR_UAPSD_MASK)
    {
        if (direction == SIR_MAC_DIRECTION_UPLINK)
            pMac->lim.gUapsdPerAcTriggerEnableMask &= ~(1 << ac);
        else if (direction == SIR_MAC_DIRECTION_DNLINK)
            pMac->lim.gUapsdPerAcDeliveryEnableMask &= ~(1 << ac);
        else if (direction == SIR_MAC_DIRECTION_BIDIR)
        {
            pMac->lim.gUapsdPerAcTriggerEnableMask &= ~(1 << ac);
            pMac->lim.gUapsdPerAcDeliveryEnableMask &= ~(1 << ac);
        }
    }
    else if (action == SET_UAPSD_MASK)
    {
        if (direction == SIR_MAC_DIRECTION_UPLINK)
            pMac->lim.gUapsdPerAcTriggerEnableMask |= (1 << ac);
        else if (direction == SIR_MAC_DIRECTION_DNLINK)
            pMac->lim.gUapsdPerAcDeliveryEnableMask |= (1 << ac);
        else if (direction == SIR_MAC_DIRECTION_BIDIR)
        {
            pMac->lim.gUapsdPerAcTriggerEnableMask |= (1 << ac);
            pMac->lim.gUapsdPerAcDeliveryEnableMask |= (1 << ac);
        }
    }

    limLog(pMac, LOG1, FL("New pMac->lim.gUapsdPerAcTriggerEnableMask = 0x%x "), pMac->lim.gUapsdPerAcTriggerEnableMask );
    limLog(pMac, LOG1, FL("New pMac->lim.gUapsdPerAcDeliveryEnableMask = 0x%x "), pMac->lim.gUapsdPerAcDeliveryEnableMask );

    return;
}

void limSetTspecUapsdMaskPerSession(tpAniSirGlobal pMac,
            tpPESession psessionEntry, tSirMacTSInfo *pTsInfo, tANI_U32 action)
{
    tANI_U8   userPrio = (tANI_U8)pTsInfo->traffic.userPrio;
    tANI_U16  direction = pTsInfo->traffic.direction;
    tANI_U8   ac = upToAc(userPrio);

    PELOG1(limLog(pMac, LOG1, FL("Set UAPSD mask for AC %d, dir %d, action=%d")
                                 ,ac, direction, action );)

    /* Converting AC to appropriate Uapsd Bit Mask
     * AC_BE(0) --> UAPSD_BITOFFSET_ACVO(3)
     * AC_BK(1) --> UAPSD_BITOFFSET_ACVO(2)
     * AC_VI(2) --> UAPSD_BITOFFSET_ACVO(1)
     * AC_VO(3) --> UAPSD_BITOFFSET_ACVO(0)
     */
    ac = ((~ac) & 0x3);

    if (action == CLEAR_UAPSD_MASK)
    {
        if (direction == SIR_MAC_DIRECTION_UPLINK)
            psessionEntry->gUapsdPerAcTriggerEnableMask &= ~(1 << ac);
        else if (direction == SIR_MAC_DIRECTION_DNLINK)
            psessionEntry->gUapsdPerAcDeliveryEnableMask &= ~(1 << ac);
        else if (direction == SIR_MAC_DIRECTION_BIDIR)
        {
            psessionEntry->gUapsdPerAcTriggerEnableMask &= ~(1 << ac);
            psessionEntry->gUapsdPerAcDeliveryEnableMask &= ~(1 << ac);
        }
    }
    else if (action == SET_UAPSD_MASK)
    {
        if (direction == SIR_MAC_DIRECTION_UPLINK)
            psessionEntry->gUapsdPerAcTriggerEnableMask |= (1 << ac);
        else if (direction == SIR_MAC_DIRECTION_DNLINK)
            psessionEntry->gUapsdPerAcDeliveryEnableMask |= (1 << ac);
        else if (direction == SIR_MAC_DIRECTION_BIDIR)
        {
            psessionEntry->gUapsdPerAcTriggerEnableMask |= (1 << ac);
            psessionEntry->gUapsdPerAcDeliveryEnableMask |= (1 << ac);
        }
    }

    limLog(pMac, LOG1,
           FL("New psessionEntry->gUapsdPerAcTriggerEnableMask = 0x%x "),
           psessionEntry->gUapsdPerAcTriggerEnableMask );
    limLog(pMac, LOG1,
           FL("New psessionEntry->gUapsdPerAcDeliveryEnableMask = 0x%x "),
           psessionEntry->gUapsdPerAcDeliveryEnableMask );

    return;
}

void limHandleHeartBeatTimeout(tpAniSirGlobal pMac )
{

    tANI_U8 i;
    for(i =0;i < pMac->lim.maxBssId;i++)
    {
        if(pMac->lim.gpSession[i].valid == TRUE )
        {
            if(pMac->lim.gpSession[i].bssType == eSIR_IBSS_MODE)
            {
                limIbssHeartBeatHandle(pMac,&pMac->lim.gpSession[i]);
                break;
            }

            if((pMac->lim.gpSession[i].bssType == eSIR_INFRASTRUCTURE_MODE) &&
                (pMac->lim.gpSession[i].limSystemRole == eLIM_STA_ROLE))
            {
                limHandleHeartBeatFailure(pMac,&pMac->lim.gpSession[i]);
            }
        }
     }
     for(i=0; i< pMac->lim.maxBssId; i++)
     {
        if(pMac->lim.gpSession[i].valid == TRUE )
        {
            if((pMac->lim.gpSession[i].bssType == eSIR_INFRASTRUCTURE_MODE) &&
                (pMac->lim.gpSession[i].limSystemRole == eLIM_STA_ROLE))
            {
                if(pMac->lim.gpSession[i].LimHBFailureStatus == eANI_BOOLEAN_TRUE)
                {
                    /* Activate Probe After HeartBeat Timer in-case
                       HB Failure detected */
                    PELOGW(limLog(pMac, LOGW,FL("Sending Probe for Session: %d"),
                            i);)
                    limDeactivateAndChangeTimer(pMac, eLIM_PROBE_AFTER_HB_TIMER);
                    MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE, 0, eLIM_PROBE_AFTER_HB_TIMER));
                    if (tx_timer_activate(&pMac->lim.limTimers.gLimProbeAfterHBTimer) != TX_SUCCESS)
                    {
                        limLog(pMac, LOGP, FL("Fail to re-activate Probe-after-heartbeat timer"));
                        limReactivateHeartBeatTimer(pMac, &pMac->lim.gpSession[i]);
                    }
                    break;
                }
            }
        }
    }
}

void limHandleHeartBeatTimeoutForSession(tpAniSirGlobal pMac, tpPESession psessionEntry)
{
    if(psessionEntry->valid == TRUE )
    {
        if(psessionEntry->bssType == eSIR_IBSS_MODE)
        {
            limIbssHeartBeatHandle(pMac,psessionEntry);
        }
        if((psessionEntry->bssType == eSIR_INFRASTRUCTURE_MODE) &&
             LIM_IS_STA_ROLE(psessionEntry)) {
            limHandleHeartBeatFailure(pMac,psessionEntry);
        }
    }
    /* In the function limHandleHeartBeatFailure things can change so check for the session entry  valid
     and the other things again */
    if(psessionEntry->valid == TRUE )
    {
        if((psessionEntry->bssType == eSIR_INFRASTRUCTURE_MODE) &&
            LIM_IS_STA_ROLE(psessionEntry)) {
            if(psessionEntry->LimHBFailureStatus == eANI_BOOLEAN_TRUE)
            {
                /* Activate Probe After HeartBeat Timer in-case
                   HB Failure detected */
                PELOGW(limLog(pMac, LOGW,FL("Sending Probe for Session: %d"),
                       psessionEntry->bssIdx);)
                limDeactivateAndChangeTimer(pMac, eLIM_PROBE_AFTER_HB_TIMER);
                MTRACE(macTrace(pMac, TRACE_CODE_TIMER_ACTIVATE, 0, eLIM_PROBE_AFTER_HB_TIMER));
                if (tx_timer_activate(&pMac->lim.limTimers.gLimProbeAfterHBTimer) != TX_SUCCESS)
                {
                    limLog(pMac, LOGP, FL("Fail to re-activate Probe-after-heartbeat timer"));
                    limReactivateHeartBeatTimer(pMac, psessionEntry);
                }
            }
        }
    }
}


tANI_U8 limGetCurrentOperatingChannel(tpAniSirGlobal pMac)
{
    tANI_U8 i;
    for(i =0;i < pMac->lim.maxBssId;i++)
    {
        if(pMac->lim.gpSession[i].valid == TRUE )
        {
            if((pMac->lim.gpSession[i].bssType == eSIR_INFRASTRUCTURE_MODE) &&
                (pMac->lim.gpSession[i].limSystemRole == eLIM_STA_ROLE))
            {
                return pMac->lim.gpSession[i].currentOperChannel;
            }
        }
    }
    return 0;
}

/**
 * limProcessAddStaRsp() - process WDA_ADD_STA_RSP from WMA
 * @mac_ctx: Pointer to Global MAC structure
 * @msg: msg from WMA
 *
 * @Return: None
 */
void limProcessAddStaRsp(tpAniSirGlobal mac_ctx, tpSirMsgQ msg)
{
	tpPESession         session;
	tpAddStaParams      add_sta_params;

	if (NULL == msg) {
		limLog(mac_ctx, LOGE, FL("NULL add_sta_rsp"));
		return;
	}

	add_sta_params = (tpAddStaParams)msg->bodyptr;
	session = peFindSessionBySessionId(mac_ctx, add_sta_params->sessionId);
	if (NULL == session) {
		limLog(mac_ctx, LOGP,
		       FL("Session does not exist for given sessionID"));
		vos_mem_free(add_sta_params);
		return;
	}
	session->csaOffloadEnable = add_sta_params->csaOffloadEnable;

	if (LIM_IS_IBSS_ROLE(session)) {
		limIbssAddStaRsp(mac_ctx, msg->bodyptr, session);
		return;
	}

	if (LIM_IS_NDI_ROLE(session)) {
		lim_ndp_add_sta_rsp(mac_ctx, session, msg->bodyptr);
		return;
	}

#ifdef FEATURE_WLAN_TDLS
	if (mac_ctx->lim.gLimAddStaTdls) {
		limProcessTdlsAddStaRsp(mac_ctx, msg->bodyptr, session);
		mac_ctx->lim.gLimAddStaTdls = FALSE;
		return;
	}
#endif

	limProcessMlmAddStaRsp(mac_ctx, msg, session);
}

void limUpdateBeacon(tpAniSirGlobal pMac)
{
    tANI_U8 i;

    for(i =0;i < pMac->lim.maxBssId;i++)
    {
        if(pMac->lim.gpSession[i].valid == TRUE )
        {
            if( ( (pMac->lim.gpSession[i].limSystemRole == eLIM_AP_ROLE) ||
                (pMac->lim.gpSession[i].limSystemRole == eLIM_STA_IN_IBSS_ROLE) )
                && (eLIM_SME_NORMAL_STATE == pMac->lim.gpSession[i].limSmeState)
               )
            {
                schSetFixedBeaconFields(pMac,&pMac->lim.gpSession[i]);
                if (VOS_FALSE == pMac->sap.SapDfsInfo.is_dfs_cac_timer_running)
                {
                    limSendBeaconInd(pMac, &pMac->lim.gpSession[i]);
                }
             }
            else
            {
                if( (pMac->lim.gpSession[i].limSystemRole == eLIM_BT_AMP_AP_ROLE)||
                    (pMac->lim.gpSession[i].limSystemRole == eLIM_BT_AMP_STA_ROLE))
                {

                    if(pMac->lim.gpSession[i].statypeForBss == STA_ENTRY_SELF)
                    {
                        schSetFixedBeaconFields(pMac,&pMac->lim.gpSession[i]);
                    }
                }
            }
        }
    }
}

void limHandleHeartBeatFailureTimeout(tpAniSirGlobal pMac)
{
    tANI_U8 i;
    tpPESession psessionEntry;
    /* Probe response is not received  after HB failure.  This is handled by LMM sub module. */
    for(i =0; i < pMac->lim.maxBssId; i++)
    {
        if(pMac->lim.gpSession[i].valid == TRUE)
        {
            psessionEntry = &pMac->lim.gpSession[i];
            if(psessionEntry->LimHBFailureStatus == eANI_BOOLEAN_TRUE)
            {
                limLog(pMac, LOGE, FL(
			"Probe_hb_failure: SME %d, MLME %d, HB Cnt %d, BCN cnt %d"),
				psessionEntry->limSmeState,
				psessionEntry->limMlmState,
				psessionEntry->LimRxedBeaconCntDuringHB,
				psessionEntry->currentBssBeaconCnt);
#ifdef FEATURE_WLAN_DIAG_SUPPORT_LIM //FEATURE_WLAN_DIAG_SUPPORT
                limDiagEventReport(pMac, WLAN_PE_DIAG_HB_FAILURE_TIMEOUT, psessionEntry, 0, 0);
#endif
                if (psessionEntry->limMlmState == eLIM_MLM_LINK_ESTABLISHED_STATE)
                {
		    /*
		     * Disconnect even if we have not received a single beacon
		     * after connection.
		     */
                    if (((!LIM_IS_CONNECTION_ACTIVE(psessionEntry)) ||
			(0 == psessionEntry->currentBssBeaconCnt)) &&
                       (psessionEntry->limSmeState != eLIM_SME_WT_DISASSOC_STATE) &&
                       (psessionEntry->limSmeState != eLIM_SME_WT_DEAUTH_STATE))
                    {
                        limLog(pMac, LOGE, FL("Probe_hb_failure: for session:%d " ),psessionEntry->peSessionId);
                        /* AP did not respond to Probe Request. Tear down link with it.*/
                        limTearDownLinkWithAp(pMac,
                                              psessionEntry->peSessionId,
                                              eSIR_BEACON_MISSED);
                        pMac->lim.gLimProbeFailureAfterHBfailedCnt++ ;
                    }
                    else // restart heartbeat timer
                    {
                        limReactivateHeartBeatTimer(pMac, psessionEntry);
                    }
                }
                else
                {
                    limLog(pMac, LOGE, FL("Unexpected wt-probe-timeout in state "));
                    limPrintMlmState(pMac, LOGE, psessionEntry->limMlmState);
                    limReactivateHeartBeatTimer(pMac, psessionEntry);
                }

            }
        }
    }
}


/*
* This function assumes there will not be more than one IBSS session active at any time.
*/
tpPESession limIsIBSSSessionActive(tpAniSirGlobal pMac)
{
    tANI_U8 i;

    for(i =0;i < pMac->lim.maxBssId;i++)
    {
        if( (pMac->lim.gpSession[i].valid) &&
             (pMac->lim.gpSession[i].limSystemRole == eLIM_STA_IN_IBSS_ROLE))
             return (&pMac->lim.gpSession[i]);
    }

    return NULL;
}

tpPESession limIsApSessionActive(tpAniSirGlobal pMac)
{
    tANI_U8 i;

    for(i =0;i < pMac->lim.maxBssId;i++)
    {
        if( (pMac->lim.gpSession[i].valid) &&
             ( (pMac->lim.gpSession[i].limSystemRole == eLIM_AP_ROLE) ||
               (pMac->lim.gpSession[i].limSystemRole == eLIM_BT_AMP_AP_ROLE)))
             return (&pMac->lim.gpSession[i]);
    }

    return NULL;
}

/**---------------------------------------------------------
\fn        limHandleDeferMsgError
\brief    handles error scenario, when the msg can not be deferred.
\param pMac
\param pLimMsg LIM msg, which could not be deferred.
\return void
-----------------------------------------------------------*/

void limHandleDeferMsgError(tpAniSirGlobal pMac, tpSirMsgQ pLimMsg)
{
    if(SIR_BB_XPORT_MGMT_MSG == pLimMsg->type)
    {
        lim_decrement_pending_mgmt_count(pMac);
        vos_pkt_return_packet((vos_pkt_t*)pLimMsg->bodyptr);
        pLimMsg->bodyptr = NULL;
    }
    else if(pLimMsg->bodyptr != NULL)
    {
        vos_mem_free(pLimMsg->bodyptr);
        pLimMsg->bodyptr = NULL;
    }

}


#ifdef FEATURE_WLAN_DIAG_SUPPORT
/**---------------------------------------------------------
\fn    limDiagEventReport
\brief This function reports Diag event
\param pMac
\param eventType
\param bssid
\param status
\param reasonCode
\return void
-----------------------------------------------------------*/
void limDiagEventReport(tpAniSirGlobal pMac, tANI_U16 eventType, tpPESession pSessionEntry, tANI_U16 status, tANI_U16 reasonCode)
{
    tSirMacAddr nullBssid = { 0, 0, 0, 0, 0, 0 };
    WLAN_VOS_DIAG_EVENT_DEF(peEvent, vos_event_wlan_pe_payload_type);

    vos_mem_set(&peEvent, sizeof(vos_event_wlan_pe_payload_type), 0);

    if (NULL == pSessionEntry)
    {
       vos_mem_copy( peEvent.bssid, nullBssid, sizeof(tSirMacAddr));
       peEvent.sme_state = (tANI_U16)pMac->lim.gLimSmeState;
       peEvent.mlm_state = (tANI_U16)pMac->lim.gLimMlmState;

    }
    else
    {
       vos_mem_copy(peEvent.bssid, pSessionEntry->bssId, sizeof(tSirMacAddr));
       peEvent.sme_state = (tANI_U16)pSessionEntry->limSmeState;
       peEvent.mlm_state = (tANI_U16)pSessionEntry->limMlmState;
    }
    peEvent.event_type = eventType;
    peEvent.status = status;
    peEvent.reason_code = reasonCode;

    WLAN_VOS_DIAG_EVENT_REPORT(&peEvent, EVENT_WLAN_PE);
    return;
}

#endif /* FEATURE_WLAN_DIAG_SUPPORT */

void limProcessAddStaSelfRsp(tpAniSirGlobal pMac,tpSirMsgQ limMsgQ)
{

   tpAddStaSelfParams      pAddStaSelfParams;
   tSirMsgQ                mmhMsg;
   tpSirSmeAddStaSelfRsp   pRsp;
   eHalStatus              status;

   pAddStaSelfParams = (tpAddStaSelfParams)limMsgQ->bodyptr;

   pRsp = vos_mem_malloc(sizeof(tSirSmeAddStaSelfRsp));
   if ( NULL == pRsp )
   {
      /// Buffer not available. Log error
      limLog(pMac, LOGP, FL("call to AllocateMemory failed for Add Sta self RSP"));
      vos_mem_free(pAddStaSelfParams);
      limMsgQ->bodyptr = NULL;
      return;
   }

   vos_mem_set((tANI_U8*)pRsp, sizeof(tSirSmeAddStaSelfRsp), 0);

   pRsp->mesgType = eWNI_SME_ADD_STA_SELF_RSP;
   pRsp->mesgLen = (tANI_U16) sizeof(tSirSmeAddStaSelfRsp);
   pRsp->status = pAddStaSelfParams->status;

   vos_mem_copy( pRsp->selfMacAddr, pAddStaSelfParams->selfMacAddr, sizeof(tSirMacAddr) );

   /*
    * For FW generated probe requests, Host needs to send Extended Capbilities
    * IE information. With this fix, Host will send the extended capabilites
    * on getting eWNI_SME_ADD_STA_SELF_RSP message(after vdev create).
    *
    * This information is required for only STA/P2P as they are the one which
    * sends probe request.
    */
   if (VOS_STATUS_SUCCESS == pRsp->status &&
       (WMI_VDEV_TYPE_STA == pAddStaSelfParams->type ||
       (WMI_VDEV_TYPE_AP == pAddStaSelfParams->type &&
        WMI_UNIFIED_VDEV_SUBTYPE_P2P_DEVICE == pAddStaSelfParams->subType))) {
        limLog(pMac, LOG1, FL("Add sta success - send ext cap IE"));
        status = lim_send_ext_cap_ie(pMac, pAddStaSelfParams->sessionId, NULL,
                                     false);
        if (eHAL_STATUS_SUCCESS != status)
            limLog(pMac, LOGE, FL("Unable to send ExtCap to FW"));
   }

   vos_mem_free(pAddStaSelfParams);
   limMsgQ->bodyptr = NULL;

   mmhMsg.type = eWNI_SME_ADD_STA_SELF_RSP;
   mmhMsg.bodyptr = pRsp;
   mmhMsg.bodyval = 0;
   MTRACE(macTrace(pMac, TRACE_CODE_TX_SME_MSG, NO_SESSION, mmhMsg.type));
   limSysProcessMmhMsgApi(pMac, &mmhMsg,  ePROT);

}

/**
 * lim_ScanTypetoString(): converts scan type enum to string.
 * @scanType: enum value of scanType.
 */
const char * lim_ScanTypetoString(const v_U8_t scanType)
{
    switch (scanType)
    {
        CASE_RETURN_STRING( eSIR_PASSIVE_SCAN );
        CASE_RETURN_STRING( eSIR_ACTIVE_SCAN );
        CASE_RETURN_STRING( eSIR_BEACON_TABLE );
        default:
            return "Unknown ScanType";
    }
}

/**
 * lim_BssTypetoString(): converts bss type enum to string.
 * @bssType: enum value of bssType.
 */

const char * lim_BssTypetoString(const v_U8_t bssType)
{
    switch (bssType)
    {
        CASE_RETURN_STRING( eSIR_INFRASTRUCTURE_MODE );
        CASE_RETURN_STRING( eSIR_INFRA_AP_MODE );
        CASE_RETURN_STRING( eSIR_IBSS_MODE );
        CASE_RETURN_STRING( eSIR_BTAMP_STA_MODE );
        CASE_RETURN_STRING( eSIR_BTAMP_AP_MODE );
        CASE_RETURN_STRING( eSIR_AUTO_MODE );
        CASE_RETURN_STRING(eSIR_NDI_MODE);
        default:
            return "Unknown BssType";
    }
}

/**
 * lim_BackgroundScanModetoString():converts BG scan type to string.
 * @mode: enum value of BG scan type.
 */

const char *lim_BackgroundScanModetoString(const v_U8_t mode)
{
    switch (mode)
    {
        CASE_RETURN_STRING( eSIR_AGGRESSIVE_BACKGROUND_SCAN );
        CASE_RETURN_STRING( eSIR_NORMAL_BACKGROUND_SCAN );
        CASE_RETURN_STRING( eSIR_ROAMING_SCAN );
        default:
            return "Unknown BgScanMode";
    }
}

void limProcessDelStaSelfRsp(tpAniSirGlobal pMac,tpSirMsgQ limMsgQ)
{

   tpDelStaSelfParams      pDelStaSelfParams;
   tSirMsgQ                mmhMsg;
   tpSirSmeDelStaSelfRsp   pRsp;


   pDelStaSelfParams = (tpDelStaSelfParams)limMsgQ->bodyptr;

   pRsp = vos_mem_malloc(sizeof(tSirSmeDelStaSelfRsp));
   if ( NULL == pRsp )
   {
      /// Buffer not available. Log error
      limLog(pMac, LOGP, FL("call to AllocateMemory failed for Add Sta self RSP"));
      vos_mem_free(pDelStaSelfParams);
      limMsgQ->bodyptr = NULL;
      return;
   }

   vos_mem_set((tANI_U8*)pRsp, sizeof(tSirSmeDelStaSelfRsp), 0);

   pRsp->mesgType = eWNI_SME_DEL_STA_SELF_RSP;
   pRsp->mesgLen = (tANI_U16) sizeof(tSirSmeDelStaSelfRsp);
   pRsp->status = pDelStaSelfParams->status;

   vos_mem_copy( pRsp->selfMacAddr, pDelStaSelfParams->selfMacAddr, sizeof(tSirMacAddr) );

   vos_mem_free(pDelStaSelfParams);
   limMsgQ->bodyptr = NULL;

   mmhMsg.type = eWNI_SME_DEL_STA_SELF_RSP;
   mmhMsg.bodyptr = pRsp;
   mmhMsg.bodyval = 0;
   MTRACE(macTrace(pMac, TRACE_CODE_TX_SME_MSG, NO_SESSION, mmhMsg.type));
   limSysProcessMmhMsgApi(pMac, &mmhMsg,  ePROT);

}

/***************************************************************
* tANI_U8 limUnmapChannel(tANI_U8 mapChannel)
* To unmap the channel to reverse the effect of mapping
* a band channel in hal .Mapping was done hal to overcome the
* limitation of the rxbd which use only 4 bit for channel number.
*****************************************************************/
tANI_U8 limUnmapChannel(tANI_U8 mapChannel)
{
   return WDA_MapChannel(mapChannel);
}


v_U8_t* limGetIEPtr(tpAniSirGlobal pMac, v_U8_t *pIes, int length, v_U8_t eid,eSizeOfLenField size_of_len_field)
{
    int left = length;
    v_U8_t *ptr = pIes;
    v_U8_t elem_id;
    v_U16_t elem_len;

    while(left >= (size_of_len_field+1))
    {
        elem_id  =  ptr[0];
        if (size_of_len_field == TWO_BYTE)
        {
            elem_len = ((v_U16_t) ptr[1]) | (ptr[2]<<8);
        }
        else
        {
            elem_len =  ptr[1];
        }


        left -= (size_of_len_field+1);
        if(elem_len > left)
        {
            limLog(pMac, LOGE,
                    FL("****Invalid IEs eid = %d elem_len=%d left=%d*****"),
                                                    eid,elem_len,left);
            return NULL;
        }
        if (elem_id == eid)
        {
            return ptr;
        }

        left -= elem_len;
        ptr += (elem_len + (size_of_len_field+1));
    }
    return NULL;
}

//Returns length of P2P stream and Pointer ie passed to this function is filled with noa stream

v_U8_t limBuildP2pIe(tpAniSirGlobal pMac, tANI_U8 *ie, tANI_U8 *data, tANI_U8 ie_len)
{
    int length = 0;
    tANI_U8 *ptr = ie;

    ptr[length++] = SIR_MAC_EID_VENDOR;
    ptr[length++] = ie_len + SIR_MAC_P2P_OUI_SIZE;
    vos_mem_copy(&ptr[length], SIR_MAC_P2P_OUI, SIR_MAC_P2P_OUI_SIZE);
    vos_mem_copy(&ptr[length + SIR_MAC_P2P_OUI_SIZE], data, ie_len);
    return (ie_len + SIR_P2P_IE_HEADER_LEN);
}

//Returns length of NoA stream and Pointer pNoaStream passed to this function is filled with noa stream

v_U8_t limGetNoaAttrStreamInMultP2pIes(tpAniSirGlobal pMac,v_U8_t* noaStream,v_U8_t noaLen,v_U8_t overFlowLen)
{
   v_U8_t overFlowP2pStream[SIR_MAX_NOA_ATTR_LEN];

   if ((noaLen <= (SIR_MAX_NOA_ATTR_LEN+SIR_P2P_IE_HEADER_LEN)) &&
       (noaLen >= overFlowLen) && (overFlowLen <= SIR_MAX_NOA_ATTR_LEN))
   {
       vos_mem_copy(overFlowP2pStream,
                     noaStream + noaLen - overFlowLen, overFlowLen);
       noaStream[noaLen - overFlowLen] = SIR_MAC_EID_VENDOR;
       noaStream[noaLen - overFlowLen + 1] = overFlowLen + SIR_MAC_P2P_OUI_SIZE;
       vos_mem_copy(noaStream+noaLen-overFlowLen + 2,
                     SIR_MAC_P2P_OUI, SIR_MAC_P2P_OUI_SIZE);
       vos_mem_copy(noaStream+noaLen + 2 + SIR_MAC_P2P_OUI_SIZE - overFlowLen,
                    overFlowP2pStream, overFlowLen);
   }

   return (noaLen + SIR_P2P_IE_HEADER_LEN);

}

//Returns length of NoA stream and Pointer pNoaStream passed to this function is filled with noa stream
v_U8_t limGetNoaAttrStream(tpAniSirGlobal pMac, v_U8_t*pNoaStream,tpPESession psessionEntry)
{
    v_U8_t len=0;

    v_U8_t   *pBody = pNoaStream;


    if   ( (psessionEntry != NULL) && (psessionEntry->valid) &&
           (psessionEntry->pePersona == VOS_P2P_GO_MODE))
    {
       if ((!(psessionEntry->p2pGoPsUpdate.uNoa1Duration)) && (!(psessionEntry->p2pGoPsUpdate.uNoa2Duration))
            && (!psessionEntry->p2pGoPsUpdate.oppPsFlag)
          )
         return 0; //No NoA Descriptor then return 0


        pBody[0] = SIR_P2P_NOA_ATTR;

        pBody[3] = psessionEntry->p2pGoPsUpdate.index;
        pBody[4] = psessionEntry->p2pGoPsUpdate.ctWin | (psessionEntry->p2pGoPsUpdate.oppPsFlag<<7);
        len = 5;
        pBody += len;


        if (psessionEntry->p2pGoPsUpdate.uNoa1Duration)
        {
            *pBody = psessionEntry->p2pGoPsUpdate.uNoa1IntervalCnt;
            pBody += 1;
            len +=1;

            *((tANI_U32 *)(pBody)) = sirSwapU32ifNeeded(psessionEntry->p2pGoPsUpdate.uNoa1Duration);
            pBody   += sizeof(tANI_U32);
            len +=4;

            *((tANI_U32 *)(pBody)) = sirSwapU32ifNeeded(psessionEntry->p2pGoPsUpdate.uNoa1Interval);
            pBody   += sizeof(tANI_U32);
            len +=4;

            *((tANI_U32 *)(pBody)) = sirSwapU32ifNeeded(psessionEntry->p2pGoPsUpdate.uNoa1StartTime);
            pBody   += sizeof(tANI_U32);
            len +=4;

        }

        if (psessionEntry->p2pGoPsUpdate.uNoa2Duration)
        {
            *pBody = psessionEntry->p2pGoPsUpdate.uNoa2IntervalCnt;
            pBody += 1;
            len +=1;

            *((tANI_U32 *)(pBody)) = sirSwapU32ifNeeded(psessionEntry->p2pGoPsUpdate.uNoa2Duration);
            pBody   += sizeof(tANI_U32);
            len +=4;

            *((tANI_U32 *)(pBody)) = sirSwapU32ifNeeded(psessionEntry->p2pGoPsUpdate.uNoa2Interval);
            pBody   += sizeof(tANI_U32);
            len +=4;

            *((tANI_U32 *)(pBody)) = sirSwapU32ifNeeded(psessionEntry->p2pGoPsUpdate.uNoa2StartTime);
            pBody   += sizeof(tANI_U32);
            len +=4;

        }


        pBody = pNoaStream + 1;
        *((tANI_U16 *)(pBody)) = sirSwapU16ifNeeded(len-3);/*one byte for Attr and 2 bytes for length*/

        return (len);

    }
    return 0;

}

void peSetResumeChannel(tpAniSirGlobal pMac, tANI_U16 channel, ePhyChanBondState phyCbState)
{

   pMac->lim.gResumeChannel = channel;
   pMac->lim.gResumePhyCbState = phyCbState;
}

/*--------------------------------------------------------------------------

  \brief peGetResumeChannel() - Returns the  channel number for scanning, from a valid session.

  This function returns the channel to resume to during link resume. channel id of 0 means HAL will
  resume to previous channel before link suspend

  \param pMac                   - pointer to global adapter context
  \return                           - channel to scan from valid session else zero.

  \sa

  --------------------------------------------------------------------------*/
void peGetResumeChannel(tpAniSirGlobal pMac, tANI_U8* resumeChannel, ePhyChanBondState* resumePhyCbState)
{

    //Rationale - this could be the suspend/resume for assoc and it is essential that
    //the new BSS is active for some time. Other BSS was anyway suspended.
    //TODO: Comeup with a better alternative. Sending NULL with PM=0 on other BSS means
    //there will be trouble. But since it is sent on current channel, it will be missed by peer
    //and hence should be ok. Need to discuss this further
    if( !limIsInMCC(pMac) )
    {
        //Get current active session channel
        peGetActiveSessionChannel(pMac, resumeChannel, resumePhyCbState);
    }
    else
    {
        *resumeChannel = pMac->lim.gResumeChannel;
        *resumePhyCbState = pMac->lim.gResumePhyCbState;
    }
    return;
}

tANI_BOOLEAN limIsNOAInsertReqd(tpAniSirGlobal pMac)
{
    tANI_U8 i;
    for(i =0; i < pMac->lim.maxBssId; i++)
    {
        if(pMac->lim.gpSession[i].valid == TRUE)
        {
            if( (eLIM_AP_ROLE == pMac->lim.gpSession[i].limSystemRole )
                    && ( VOS_P2P_GO_MODE == pMac->lim.gpSession[i].pePersona )
                   )
            {
                return TRUE;
            }
        }
    }
    return FALSE;
}


tANI_BOOLEAN limIsconnectedOnDFSChannel(tANI_U8 currentChannel)
{
    if(NV_CHANNEL_DFS == vos_nv_getChannelEnabledState(currentChannel))
    {
        return eANI_BOOLEAN_TRUE;
    }
    else
    {
        return eANI_BOOLEAN_FALSE;
    }
}

#ifdef WLAN_FEATURE_11W
void limPmfSaQueryTimerHandler(void *pMacGlobal, tANI_U32 param)
{
    tpAniSirGlobal pMac = (tpAniSirGlobal)pMacGlobal;
    tPmfSaQueryTimerId timerId;
    tpPESession psessionEntry;
    tpDphHashNode pSta;
    tANI_U32 maxRetries;

    limLog(pMac, LOG1, FL("SA Query timer fires"));
    timerId.value = param;

    // Check that SA Query is in progress
    if ((psessionEntry = peFindSessionBySessionId(
        pMac, timerId.fields.sessionId)) == NULL)
    {
        limLog(pMac, LOGE, FL("Session does not exist for given session ID %d"),
               timerId.fields.sessionId);
        return;
    }
    if ((pSta = dphGetHashEntry(pMac, timerId.fields.peerIdx,
                                &psessionEntry->dph.dphHashTable)) == NULL)
    {
        limLog(pMac, LOGE, FL("Entry does not exist for given peer index %d"),
               timerId.fields.peerIdx);
        return;
    }
    if (DPH_SA_QUERY_IN_PROGRESS != pSta->pmfSaQueryState)
        return;

    // Increment the retry count, check if reached maximum
    if (wlan_cfgGetInt(pMac, WNI_CFG_PMF_SA_QUERY_MAX_RETRIES,
                       &maxRetries) != eSIR_SUCCESS)
    {
        limLog(pMac, LOGE, FL("Could not retrieve PMF SA Query maximum retries value"));
        pSta->pmfSaQueryState = DPH_SA_QUERY_NOT_IN_PROGRESS;
        return;
    }
    pSta->pmfSaQueryRetryCount++;
    if (pSta->pmfSaQueryRetryCount >= maxRetries)
    {
        limLog(pMac, LOGE, FL("SA Query timed out,Deleting STA"));
        limPrintMacAddr(pMac, pSta->staAddr, LOGE);
        limSendDisassocMgmtFrame(pMac,
                  eSIR_MAC_DISASSOC_DUE_TO_INACTIVITY_REASON,
                  pSta->staAddr, psessionEntry, FALSE);
        limTriggerSTAdeletion(pMac, pSta, psessionEntry);
        pSta->pmfSaQueryState = DPH_SA_QUERY_TIMED_OUT;
        return;
    }

    // Retry SA Query
    limSendSaQueryRequestFrame(pMac, (tANI_U8 *)&(pSta->pmfSaQueryCurrentTransId),
                               pSta->staAddr, psessionEntry);
    pSta->pmfSaQueryCurrentTransId++;
    limLog(pMac, LOGE, FL("Starting SA Query retry %d"), pSta->pmfSaQueryRetryCount);
    if (tx_timer_activate(&pSta->pmfSaQueryTimer) != TX_SUCCESS)
    {
        limLog(pMac, LOGE, FL("PMF SA Query timer activation failed!"));
        pSta->pmfSaQueryState = DPH_SA_QUERY_NOT_IN_PROGRESS;
    }
}
#endif


#ifdef WLAN_FEATURE_11AC
tANI_BOOLEAN limCheckVHTOpModeChange( tpAniSirGlobal pMac, tpPESession psessionEntry,
                                      tANI_U8 chanWidth, tANI_U8 chanMode,
                                      tANI_U8 staId, tANI_U8 *peerMac)
{
    tUpdateVHTOpMode tempParam;

    tempParam.opMode = chanWidth;
    tempParam.chanMode = chanMode;
    tempParam.staId  = staId;
    tempParam.smesessionId = psessionEntry->smeSessionId;
    vos_mem_copy(tempParam.peer_mac, peerMac,
                 sizeof(tSirMacAddr));

    limSendModeUpdate( pMac, &tempParam, psessionEntry );

    return eANI_BOOLEAN_TRUE;
}

tANI_BOOLEAN limSetNssChange( tpAniSirGlobal pMac, tpPESession psessionEntry, tANI_U8 rxNss,
                              tANI_U8 staId, tANI_U8 *peerMac)
{
    tUpdateRxNss tempParam;

    tempParam.rxNss = rxNss;
    tempParam.staId  = staId;
    tempParam.smesessionId = psessionEntry->smeSessionId;
    vos_mem_copy(tempParam.peer_mac, peerMac,
                 sizeof(tSirMacAddr));

    limSendRxNssUpdate( pMac, &tempParam, psessionEntry );

    return eANI_BOOLEAN_TRUE;
}

tANI_BOOLEAN limCheckMembershipUserPosition( tpAniSirGlobal pMac, tpPESession psessionEntry,
                                             tANI_U32 membership, tANI_U32 userPosition,
                                             tANI_U8 staId)
{
    tUpdateMembership tempParamMembership;
    tUpdateUserPos tempParamUserPosition;

    tempParamMembership.membership = membership;
    tempParamMembership.staId  = staId;
    tempParamMembership.smesessionId = psessionEntry->smeSessionId;
    vos_mem_copy(tempParamMembership.peer_mac, psessionEntry->bssId,
                   sizeof( tSirMacAddr ));


    limSetMembership( pMac, &tempParamMembership, psessionEntry );

    tempParamUserPosition.userPos = userPosition;
    tempParamUserPosition.staId  = staId;
    tempParamUserPosition.smesessionId = psessionEntry->smeSessionId;
    vos_mem_copy(tempParamUserPosition.peer_mac, psessionEntry->bssId,
                   sizeof( tSirMacAddr ));


    limSetUserPos( pMac, &tempParamUserPosition, psessionEntry );

    return eANI_BOOLEAN_TRUE;
}
#endif

void limGetShortSlotFromPhyMode(tpAniSirGlobal pMac, tpPESession psessionEntry,
                                   tANI_U32 phyMode, tANI_U8 *pShortSlotEnabled)
{
    tANI_U8 val=0;

    //only 2.4G band should have short slot enable, rest it should be default
    if (phyMode == WNI_CFG_PHY_MODE_11G)
    {
        /* short slot is default in all other modes */
        if ((psessionEntry->pePersona == VOS_STA_SAP_MODE) ||
            (psessionEntry->pePersona == VOS_IBSS_MODE) ||
            (psessionEntry->pePersona == VOS_P2P_GO_MODE))
        {
            val = true;
        }
        if (psessionEntry->limMlmState == eLIM_MLM_WT_JOIN_BEACON_STATE)
        {
            // Joining BSS.
            val = SIR_MAC_GET_SHORT_SLOT_TIME( psessionEntry->limCurrentBssCaps);
        }
        else if (psessionEntry->limMlmState == eLIM_MLM_WT_REASSOC_RSP_STATE)
        {
            // Reassociating with AP.
            val = SIR_MAC_GET_SHORT_SLOT_TIME( psessionEntry->limReassocBssCaps);
        }
    }
    else
    {
        /*
         * 11B does not short slot and short slot is default
         * for 11A mode. Hence, not need to set this bit
         */
        val = false;
    }

    limLog(pMac, LOG1, FL("phyMode = %u shortslotsupported = %u"), phyMode, val);
    *pShortSlotEnabled = val;
}

void limUtilsframeshtons(tpAniSirGlobal    pCtx,
                            tANI_U8  *pOut,
                            tANI_U16  pIn,
                            tANI_U8  fMsb)
{
    (void)pCtx;
#if defined ( DOT11F_LITTLE_ENDIAN_HOST )
    if ( !fMsb )
    {
        DOT11F_MEMCPY(pCtx, pOut, &pIn, 2);
    }
    else
    {
        *pOut         = ( pIn & 0xff00 ) >> 8;
        *( pOut + 1 ) = pIn & 0xff;
    }
#else
    if ( !fMsb )
    {
        *pOut         = pIn & 0xff;
        *( pOut + 1 ) = ( pIn & 0xff00 ) >> 8;
    }
    else
    {
        DOT11F_MEMCPY(pCtx, pOut, &pIn, 2);
    }
#endif
}

void limUtilsframeshtonl(tpAniSirGlobal    pCtx,
                        tANI_U8  *pOut,
                        tANI_U32  pIn,
                        tANI_U8  fMsb)
{
    (void)pCtx;
#if defined ( DOT11F_LITTLE_ENDIAN_HOST )
    if ( !fMsb )
    {
        DOT11F_MEMCPY(pCtx, pOut, &pIn, 4);
    }
    else
    {
        *pOut         = ( pIn & 0xff000000 ) >> 24;
        *( pOut + 1 ) = ( pIn & 0x00ff0000 ) >> 16;
        *( pOut + 2 ) = ( pIn & 0x0000ff00 ) >>  8;
        *( pOut + 3 ) = ( pIn & 0x000000ff );
    }
#else
    if ( !fMsb )
    {
        *( pOut     ) = ( pIn & 0x000000ff );
        *( pOut + 1 ) = ( pIn & 0x0000ff00 ) >>  8;
        *( pOut + 2 ) = ( pIn & 0x00ff0000 ) >> 16;
        *( pOut + 3 ) = ( pIn & 0xff000000 ) >> 24;
    }
    else
    {
        DOT11F_MEMCPY(pCtx, pOut, &pIn, 4);
    }
#endif
}

#ifdef WLAN_FEATURE_11W
/**
 *
 * \brief This function is called by various LIM modules to correctly set
 * the Protected bit in the Frame Control Field of the 802.11 frame MAC header
 *
 *
 * \param  pMac Pointer to Global MAC structure
 *
 * \param psessionEntry Pointer to session corresponding to the connection
 *
 * \param peer Peer address of the STA to which the frame is to be sent
 *
 * \param pMacHdr Pointer to the frame MAC header
 *
 * \return nothing
 *
 *
 */
void
limSetProtectedBit(tpAniSirGlobal  pMac,
                   tpPESession     psessionEntry,
                   tSirMacAddr     peer,
                   tpSirMacMgmtHdr pMacHdr)
{
    tANI_U16 aid;
    tpDphHashNode pStaDs;

    if (LIM_IS_AP_ROLE(psessionEntry) || LIM_IS_BT_AMP_AP_ROLE(psessionEntry)) {
        pStaDs = dphLookupHashEntry( pMac, peer, &aid,
                                     &psessionEntry->dph.dphHashTable );
        if( pStaDs != NULL )
            /* rmfenabled will be set at the time of addbss.
             * but sometimes EAP auth fails and keys are not
             * installed then if we send any management frame
             * like deauth/disassoc with this bit set then
             * firmware crashes. so check for keys are
             * installed or not also before setting the bit
             */
            if (pStaDs->rmfEnabled && pStaDs->isKeyInstalled)
                pMacHdr->fc.wep = 1;
    }
    else if ( psessionEntry->limRmfEnabled && psessionEntry->isKeyInstalled)
        pMacHdr->fc.wep = 1;
} /*** end limSetProtectedBit() ***/
#endif

tANI_U8* lim_get_ie_ptr(tANI_U8 *pIes, int length, tANI_U8 eid)
{
    int left = length;
    tANI_U8 *ptr = pIes;
    tANI_U8 elem_id, elem_len;

    while(left >= 2)
    {
        elem_id  =  ptr[0];
        elem_len =  ptr[1];
        left -= 2;
        if(elem_len > left)
        {
            VOS_TRACE(VOS_MODULE_ID_PE, VOS_TRACE_LEVEL_ERROR,
                FL("****Invalid IEs eid = %d elem_len=%d left=%d*****"),
                eid,elem_len, left);

            return NULL;
        }
        if (elem_id == eid)
        {
            return ptr;
        }

        left -= elem_len;
        ptr += (elem_len + 2);
    }
    return NULL;
}

void lim_set_ht_caps(tpAniSirGlobal p_mac, tpPESession p_session_entry,
                     tANI_U8 *p_ie_start,tANI_U32 num_bytes)
{
    v_U8_t              *p_ie=NULL;
    tDot11fIEHTCaps     dot11_ht_cap = {0,};

    PopulateDot11fHTCaps(p_mac, p_session_entry, &dot11_ht_cap);
    p_ie = limGetIEPtr(p_mac, p_ie_start, num_bytes, DOT11F_EID_HTCAPS,
                                                    ONE_BYTE);
    limLog( p_mac, LOG2, FL("p_ie %pK dot11_ht_cap.supportedMCSSet[0]=0x%x"),
            p_ie, dot11_ht_cap.supportedMCSSet[0]);

    if(p_ie)
    {
        /* convert from unpacked to packed structure */
        tHtCaps *p_ht_cap = (tHtCaps *)&p_ie[2];

        p_ht_cap->advCodingCap = dot11_ht_cap.advCodingCap;
        p_ht_cap->supportedChannelWidthSet =
            dot11_ht_cap.supportedChannelWidthSet;
        p_ht_cap->mimoPowerSave = dot11_ht_cap.mimoPowerSave;
        p_ht_cap->greenField = dot11_ht_cap.greenField;
        p_ht_cap->shortGI20MHz = dot11_ht_cap.shortGI20MHz;
        p_ht_cap->shortGI40MHz = dot11_ht_cap.shortGI40MHz;
        p_ht_cap->txSTBC = dot11_ht_cap.txSTBC;
        p_ht_cap->rxSTBC = dot11_ht_cap.rxSTBC;
        p_ht_cap->delayedBA = dot11_ht_cap.delayedBA  ;
        p_ht_cap->maximalAMSDUsize = dot11_ht_cap.maximalAMSDUsize;
        p_ht_cap->dsssCckMode40MHz = dot11_ht_cap.dsssCckMode40MHz;
        p_ht_cap->psmp = dot11_ht_cap.psmp;
        p_ht_cap->stbcControlFrame = dot11_ht_cap.stbcControlFrame;
        p_ht_cap->lsigTXOPProtection = dot11_ht_cap.lsigTXOPProtection;
        p_ht_cap->maxRxAMPDUFactor = dot11_ht_cap.maxRxAMPDUFactor;
        p_ht_cap->mpduDensity = dot11_ht_cap.mpduDensity;
        vos_mem_copy((void *)p_ht_cap->supportedMCSSet,
                     (void *)(dot11_ht_cap.supportedMCSSet),
                      sizeof(p_ht_cap->supportedMCSSet));
        p_ht_cap->pco = dot11_ht_cap.pco;
        p_ht_cap->transitionTime = dot11_ht_cap.transitionTime;
        p_ht_cap->mcsFeedback = dot11_ht_cap.mcsFeedback;
        p_ht_cap->txBF = dot11_ht_cap.txBF;
        p_ht_cap->rxStaggeredSounding = dot11_ht_cap.rxStaggeredSounding;
        p_ht_cap->txStaggeredSounding = dot11_ht_cap.txStaggeredSounding;
        p_ht_cap->rxZLF = dot11_ht_cap.rxZLF;
        p_ht_cap->txZLF = dot11_ht_cap.txZLF;
        p_ht_cap->implicitTxBF = dot11_ht_cap.implicitTxBF;
        p_ht_cap->calibration = dot11_ht_cap.calibration;
        p_ht_cap->explicitCSITxBF = dot11_ht_cap.explicitCSITxBF;
        p_ht_cap->explicitUncompressedSteeringMatrix =
            dot11_ht_cap.explicitUncompressedSteeringMatrix;
        p_ht_cap->explicitBFCSIFeedback = dot11_ht_cap.explicitBFCSIFeedback;
        p_ht_cap->explicitUncompressedSteeringMatrixFeedback =
            dot11_ht_cap.explicitUncompressedSteeringMatrixFeedback;
        p_ht_cap->explicitCompressedSteeringMatrixFeedback =
            dot11_ht_cap.explicitCompressedSteeringMatrixFeedback;
        p_ht_cap->csiNumBFAntennae = dot11_ht_cap.csiNumBFAntennae;
        p_ht_cap->uncompressedSteeringMatrixBFAntennae =
            dot11_ht_cap.uncompressedSteeringMatrixBFAntennae;
        p_ht_cap->compressedSteeringMatrixBFAntennae =
            dot11_ht_cap.compressedSteeringMatrixBFAntennae;
        p_ht_cap->antennaSelection = dot11_ht_cap.antennaSelection;
        p_ht_cap->explicitCSIFeedbackTx = dot11_ht_cap.explicitCSIFeedbackTx;
        p_ht_cap->antennaIndicesFeedbackTx =
            dot11_ht_cap.antennaIndicesFeedbackTx;
        p_ht_cap->explicitCSIFeedback = dot11_ht_cap.explicitCSIFeedback;
        p_ht_cap->antennaIndicesFeedback = dot11_ht_cap.antennaIndicesFeedback;
        p_ht_cap->rxAS = dot11_ht_cap.rxAS;
        p_ht_cap->txSoundingPPDUs = dot11_ht_cap.txSoundingPPDUs;
    }
}

#ifdef WLAN_FEATURE_11AC
void lim_set_vht_caps(tpAniSirGlobal p_mac, tpPESession p_session_entry,
                     tANI_U8 *p_ie_start,tANI_U32 num_bytes)
{
    v_U8_t              *p_ie=NULL;
    tDot11fIEVHTCaps     dot11_vht_cap;

    PopulateDot11fVHTCaps(p_mac, p_session_entry, &dot11_vht_cap);
    p_ie = limGetIEPtr(p_mac, p_ie_start, num_bytes, DOT11F_EID_VHTCAPS,
                       ONE_BYTE);

    if(p_ie) {
        tSirMacVHTCapabilityInfo *vht_cap =
                                  (tSirMacVHTCapabilityInfo *) &p_ie[2];
        tSirVhtMcsInfo *vht_mcs = (tSirVhtMcsInfo *)
                                  &p_ie[2 + sizeof(tSirMacVHTCapabilityInfo)];
        union {
            tANI_U16                       u_value;
            tSirMacVHTRxSupDataRateInfo    vht_rx_supp_rate;
            tSirMacVHTTxSupDataRateInfo    vht_tx_supp_rate;
        } u_vht_data_rate_info;


        vht_cap->maxMPDULen = dot11_vht_cap.maxMPDULen;
        vht_cap->supportedChannelWidthSet =
                               dot11_vht_cap.supportedChannelWidthSet;
        vht_cap->ldpcCodingCap = dot11_vht_cap.ldpcCodingCap;
        vht_cap->shortGI80MHz = dot11_vht_cap.shortGI80MHz;
        vht_cap->shortGI160and80plus80MHz =
                               dot11_vht_cap.shortGI160and80plus80MHz;
        vht_cap->txSTBC = dot11_vht_cap.txSTBC;
        vht_cap->rxSTBC = dot11_vht_cap.rxSTBC;
        vht_cap->suBeamFormerCap = dot11_vht_cap.suBeamFormerCap;
        vht_cap->suBeamformeeCap = dot11_vht_cap.suBeamformeeCap;
        vht_cap->csnofBeamformerAntSup = dot11_vht_cap.csnofBeamformerAntSup;
        vht_cap->numSoundingDim = dot11_vht_cap.numSoundingDim;
        vht_cap->muBeamformerCap = dot11_vht_cap.muBeamformerCap;
        vht_cap->muBeamformeeCap = dot11_vht_cap.muBeamformeeCap;
        vht_cap->vhtTXOPPS = dot11_vht_cap.vhtTXOPPS;
        vht_cap->htcVHTCap = dot11_vht_cap.htcVHTCap;
        vht_cap->maxAMPDULenExp = dot11_vht_cap.maxAMPDULenExp;
        vht_cap->vhtLinkAdaptCap = dot11_vht_cap.vhtLinkAdaptCap;
        vht_cap->rxAntPattern = dot11_vht_cap.rxAntPattern;
        vht_cap->txAntPattern = dot11_vht_cap.txAntPattern;
        vht_cap->reserved1 = dot11_vht_cap.reserved1;

        /* Populate VHT MCS Information */
        vht_mcs->rxMcsMap = dot11_vht_cap.rxMCSMap;
        u_vht_data_rate_info.vht_rx_supp_rate.rxSupDataRate =
                            dot11_vht_cap.rxHighSupDataRate;
        u_vht_data_rate_info.vht_rx_supp_rate.reserved =
                            dot11_vht_cap.reserved2;
        vht_mcs->rxHighest = u_vht_data_rate_info.u_value;

        vht_mcs->txMcsMap = dot11_vht_cap.txMCSMap;
        u_vht_data_rate_info.vht_tx_supp_rate.txSupDataRate =
                            dot11_vht_cap.txSupDataRate;
        u_vht_data_rate_info.vht_tx_supp_rate.reserved =
                            dot11_vht_cap.reserved3;
        vht_mcs->txHighest = u_vht_data_rate_info.u_value;
    }
}
#endif /* WLAN_FEATURE_11AC */

#ifdef SAP_AUTH_OFFLOAD
static tpDphHashNode
_sap_offload_parse_assoc_req(tpAniSirGlobal pmac,
                    tpSirAssocReq assoc_req,
                    struct sap_offload_add_sta_req *add_sta_req, bool *pinuse)
{
    tpSirMacAssocReqFrame mac_assoc_req = NULL;
    tpSirAssocReq temp_assoc_req;
    tSirRetStatus status;
    tpSirMacMgmtHdr mac_hdr = NULL;
    tpDphHashNode sta_ds = NULL;
    uint8_t *frame_body;

    tpPESession session_entry = limIsApSessionActive(pmac);
    mac_hdr = (tpSirMacMgmtHdr)add_sta_req->conn_req;

    if (dph_entry_exist(pmac,
                        mac_hdr->sa,
                        add_sta_req->assoc_id,
                        &session_entry->dph.dphHashTable)) {
        *pinuse = true;
        return NULL;
    }

    /* Update Attribute and Remove IE for
     * Software AP Authentication Offload
     */
    frame_body = (tANI_U8 *)add_sta_req->conn_req + sizeof(*mac_hdr);
    mac_assoc_req = (tpSirMacAssocReqFrame)frame_body;
    mac_assoc_req->capabilityInfo.privacy = 0;

    if (mac_hdr->fc.subType == SIR_MAC_MGMT_ASSOC_REQ) {
        status = sirConvertAssocReqFrame2Struct(pmac,
                                            frame_body,
                                            add_sta_req->conn_req_len,
                                            assoc_req);
    } else {
        status = sirConvertReassocReqFrame2Struct(pmac,
                                            frame_body,
                                            add_sta_req->conn_req_len,
                                            assoc_req);
    }

    if (status != eSIR_SUCCESS) {
        limLog(pmac, LOGW, FL("sap_offload_add_sta_req parse error\n"));
        goto error;
    }
    /* For software AP Auth Offload feature
     * Host will take it as none security station
     * Force change to none security
     */
    assoc_req->rsnPresent = 0;
    assoc_req->wpaPresent = 0;

    sta_ds = dphAddHashEntry(pmac,
                             mac_hdr->sa,
                             add_sta_req->assoc_id,
                             &session_entry->dph.dphHashTable);
    if (sta_ds == NULL) {
        /* Could not add hash table entry at DPH */
        limLog(pmac, LOGE,
           FL("could not add hash entry at DPH for aid=%d, MacAddr:"
               MAC_ADDRESS_STR),
           add_sta_req->assoc_id,MAC_ADDR_ARRAY(mac_hdr->sa));
        goto error;
    }

    if (session_entry->parsedAssocReq != NULL) {
        temp_assoc_req = session_entry->parsedAssocReq[sta_ds->assocId];
        if (temp_assoc_req != NULL) {
            if (temp_assoc_req->assocReqFrame) {
                vos_mem_free(temp_assoc_req->assocReqFrame);
                temp_assoc_req->assocReqFrame = NULL;
                temp_assoc_req->assocReqFrameLength = 0;
            }
            vos_mem_free(temp_assoc_req);
            temp_assoc_req = NULL;
        }
        session_entry->parsedAssocReq[sta_ds->assocId] = assoc_req;
    }
error:
    return sta_ds;
}

static void
_sap_offload_parse_sta_capability(tpDphHashNode sta_ds,
                        tpSirAssocReq assoc_req,
                        struct sap_offload_add_sta_req *add_sta_req)
{
    tpSirMacMgmtHdr mac_hdr = NULL;

    mac_hdr = (tpSirMacMgmtHdr)add_sta_req->conn_req;

    sta_ds->mlmStaContext.htCapability = assoc_req->HTCaps.present;
#ifdef WLAN_FEATURE_11AC
    sta_ds->mlmStaContext.vhtCapability = assoc_req->VHTCaps.present;
#endif
    sta_ds->qos.addtsPresent = (assoc_req->addtsPresent==0) ? false : true;
    sta_ds->qos.addts        = assoc_req->addtsReq;
    sta_ds->qos.capability   = assoc_req->qosCapability;
    sta_ds->versionPresent   = 0;
    /* short slot and short preamble should be
     * updated before doing limaddsta
     */
    sta_ds->shortPreambleEnabled =
        (tANI_U8)assoc_req->capabilityInfo.shortPreamble;
    sta_ds->shortSlotTimeEnabled =
        (tANI_U8)assoc_req->capabilityInfo.shortSlotTime;

    sta_ds->valid = 0;
    /* The Auth Type of Software AP Authentication Offload
     * is always Open System is host side
     */
    sta_ds->mlmStaContext.authType = eSIR_OPEN_SYSTEM;
    sta_ds->staType = STA_ENTRY_PEER;

    /* Re/Assoc Response frame to requesting STA */
    sta_ds->mlmStaContext.subType = mac_hdr->fc.subType;

    sta_ds->mlmStaContext.listenInterval = assoc_req->listenInterval;
    sta_ds->mlmStaContext.capabilityInfo = assoc_req->capabilityInfo;

    /* The following count will be used to knock-off the station
     * if it doesn't come back to receive the buffered data.
     * The AP will wait for numTimSent number of beacons after
     * sending TIM information for the station, before assuming that
     * the station is no more associated and disassociates it
     */

    /* timWaitCount is used by PMM for monitoring the STA's in PS for LINK*/
    sta_ds->timWaitCount =
        (tANI_U8)GET_TIM_WAIT_COUNT(assoc_req->listenInterval);

    /* Initialise the Current successful
     * MPDU's tranfered to this STA count as 0
     */
    sta_ds->curTxMpduCnt = 0;
}

static tSirRetStatus
_sap_offload_parse_sta_vht(tpAniSirGlobal pmac,
                        tpDphHashNode sta_ds,
                        tpSirAssocReq assoc_req)
{
    tpPESession session_entry = limIsApSessionActive(pmac);

    if (IS_DOT11_MODE_HT(session_entry->dot11mode) &&
        assoc_req->HTCaps.present && assoc_req->wmeInfoPresent) {
        sta_ds->htGreenfield = (tANI_U8)assoc_req->HTCaps.greenField;
        sta_ds->htAMpduDensity = assoc_req->HTCaps.mpduDensity;
        sta_ds->htDsssCckRate40MHzSupport =
            (tANI_U8)assoc_req->HTCaps.dsssCckMode40MHz;
        sta_ds->htLsigTXOPProtection =
            (tANI_U8)assoc_req->HTCaps.lsigTXOPProtection;
        sta_ds->htMaxAmsduLength =
            (tANI_U8)assoc_req->HTCaps.maximalAMSDUsize;
        sta_ds->htMaxRxAMpduFactor = assoc_req->HTCaps.maxRxAMPDUFactor;
        sta_ds->htMIMOPSState = assoc_req->HTCaps.mimoPowerSave;
        sta_ds->htShortGI20Mhz = (tANI_U8)assoc_req->HTCaps.shortGI20MHz;
        sta_ds->htShortGI40Mhz = (tANI_U8)assoc_req->HTCaps.shortGI40MHz;
        sta_ds->htSupportedChannelWidthSet =
            (tANI_U8)assoc_req->HTCaps.supportedChannelWidthSet;
        /* peer just follows AP; so when we are softAP/GO,
         * we just store our session entry's secondary channel offset here
         * in peer INFRA STA. However, if peer's 40MHz channel width support
         * is disabled then secondary channel will be zero
         */
        sta_ds->htSecondaryChannelOffset =
            (sta_ds->htSupportedChannelWidthSet) ?
            session_entry->htSecondaryChannelOffset : 0;
#ifdef WLAN_FEATURE_11AC
        if (assoc_req->operMode.present) {
            sta_ds->vhtSupportedChannelWidthSet =
                (tANI_U8)((assoc_req->operMode.chanWidth ==
                eHT_CHANNEL_WIDTH_80MHZ) ?
                    WNI_CFG_VHT_CHANNEL_WIDTH_80MHZ :
                    WNI_CFG_VHT_CHANNEL_WIDTH_20_40MHZ);
            sta_ds->htSupportedChannelWidthSet =
                (tANI_U8)(assoc_req->operMode.chanWidth ?
                    eHT_CHANNEL_WIDTH_40MHZ : eHT_CHANNEL_WIDTH_20MHZ);
        } else if (assoc_req->VHTCaps.present) {
            /* Check if STA has enabled it's channel bonding mode.
             * If channel bonding mode is enabled, we decide based on
             * SAP's current configuration else, we set it to VHT20.
             */
            sta_ds->vhtSupportedChannelWidthSet =
                (tANI_U8)((sta_ds->htSupportedChannelWidthSet ==
                eHT_CHANNEL_WIDTH_20MHZ) ?
                WNI_CFG_VHT_CHANNEL_WIDTH_20_40MHZ :
                session_entry->vhtTxChannelWidthSet );
            sta_ds->htMaxRxAMpduFactor = assoc_req->VHTCaps.maxAMPDULenExp;
        }

        /* Lesser among the AP and STA bandwidth of operation. */
        sta_ds->htSupportedChannelWidthSet =
            (sta_ds->htSupportedChannelWidthSet <
            session_entry->htSupportedChannelWidthSet) ?
            sta_ds->htSupportedChannelWidthSet :
            session_entry->htSupportedChannelWidthSet ;
#endif
        sta_ds->baPolicyFlag = 0xFF;
        sta_ds->htLdpcCapable = (tANI_U8)assoc_req->HTCaps.advCodingCap;
    }

    if (assoc_req->VHTCaps.present && assoc_req->wmeInfoPresent) {
        sta_ds->vhtLdpcCapable = (tANI_U8)assoc_req->VHTCaps.ldpcCodingCap;
    }

    if (!assoc_req->wmeInfoPresent) {
        sta_ds->mlmStaContext.htCapability = 0;
#ifdef WLAN_FEATURE_11AC
        sta_ds->mlmStaContext.vhtCapability = 0;
#endif
    }
#ifdef WLAN_FEATURE_11AC
    if (limPopulateMatchingRateSet(pmac,
                                   sta_ds,
                                   &(assoc_req->supportedRates),
                                   &(assoc_req->extendedRates),
                                   assoc_req->HTCaps.supportedMCSSet,
                                   session_entry , &assoc_req->VHTCaps)
                                   != eSIR_SUCCESS) {
#else
    if (limPopulateMatchingRateSet(pmac,
                                   sta_ds,
                                   &(assoc_req->supportedRates),
                                   &(assoc_req->extendedRates),
                                   assoc_req->HTCaps.supportedMCSSet,
                                   &(assoc_req->propIEinfo.propRates),
                                   session_entry) != eSIR_SUCCESS) {
#endif
        limLog(pmac, LOGE,
           FL("Rate set mismatched for aid=%d, MacAddr: "
           MAC_ADDRESS_STR),
           sta_ds->assocId, MAC_ADDR_ARRAY(sta_ds->staAddr));
        goto error;
    }

#ifdef WLAN_FEATURE_11AC
    if (assoc_req->operMode.present) {
        sta_ds->vhtSupportedRxNss = assoc_req->operMode.rxNSS + 1;
    } else {
        sta_ds->vhtSupportedRxNss =
            ((sta_ds->supportedRates.vhtRxMCSMap & MCSMAPMASK2x2)
            == MCSMAPMASK2x2) ? 1 : 2;
    }
#endif

    return eSIR_SUCCESS;
error:
    return eSIR_FAILURE;
}

static void
_sap_offload_parse_sta_qos(tpAniSirGlobal pmac,
                        tpDphHashNode sta_ds,
                        tpSirAssocReq assoc_req)
{
    tHalBitVal qos_mode;
    tHalBitVal wsm_mode, wme_mode;
    tpPESession session_entry = limIsApSessionActive(pmac);

    limGetQosMode(session_entry, &qos_mode);
    sta_ds->qosMode    = eANI_BOOLEAN_FALSE;
    sta_ds->lleEnabled = eANI_BOOLEAN_FALSE;

    if (assoc_req->capabilityInfo.qos && (qos_mode == eHAL_SET)) {
        sta_ds->lleEnabled = eANI_BOOLEAN_TRUE;
        sta_ds->qosMode    = eANI_BOOLEAN_TRUE;
    }

    sta_ds->wmeEnabled = eANI_BOOLEAN_FALSE;
    sta_ds->wsmEnabled = eANI_BOOLEAN_FALSE;
    limGetWmeMode(session_entry, &wme_mode);
    if ((!sta_ds->lleEnabled) && assoc_req->wmeInfoPresent &&
        (wme_mode == eHAL_SET)) {
        sta_ds->wmeEnabled = eANI_BOOLEAN_TRUE;
        sta_ds->qosMode = eANI_BOOLEAN_TRUE;
        limGetWsmMode(session_entry, &wsm_mode);
        /* WMM_APSD - WMM_SA related processing should be
         * separate; WMM_SA and WMM_APSD can coexist
         */
        if (assoc_req->WMMInfoStation.present) {
            /* check whether AP supports or not */
            if ((session_entry->limSystemRole == eLIM_AP_ROLE)
                 && (session_entry->apUapsdEnable == 0) &&
                    (assoc_req->WMMInfoStation.acbe_uapsd
                    || assoc_req->WMMInfoStation.acbk_uapsd
                    || assoc_req->WMMInfoStation.acvo_uapsd
                    || assoc_req->WMMInfoStation.acvi_uapsd)) {
                /*
                 * Received Re/Association Request from
                 * STA when UPASD is not supported
                 */
               limLog( pmac, LOGE, FL( "AP do not support UAPSD so reply "
                                       "to STA accordingly" ));
               /* update UAPSD and send it to LIM to add STA */
               sta_ds->qos.capability.qosInfo.acbe_uapsd = 0;
               sta_ds->qos.capability.qosInfo.acbk_uapsd = 0;
               sta_ds->qos.capability.qosInfo.acvo_uapsd = 0;
               sta_ds->qos.capability.qosInfo.acvi_uapsd = 0;
               sta_ds->qos.capability.qosInfo.maxSpLen =   0;
            } else {
                /* update UAPSD and send it to LIM to add STA */
                sta_ds->qos.capability.qosInfo.acbe_uapsd =
                    assoc_req->WMMInfoStation.acbe_uapsd;
                sta_ds->qos.capability.qosInfo.acbk_uapsd =
                    assoc_req->WMMInfoStation.acbk_uapsd;
                sta_ds->qos.capability.qosInfo.acvo_uapsd =
                    assoc_req->WMMInfoStation.acvo_uapsd;
                sta_ds->qos.capability.qosInfo.acvi_uapsd =
                    assoc_req->WMMInfoStation.acvi_uapsd;
                sta_ds->qos.capability.qosInfo.maxSpLen =
                    assoc_req->WMMInfoStation.max_sp_length;
            }
        }
        if (assoc_req->wsmCapablePresent && (wsm_mode == eHAL_SET))
            sta_ds->wsmEnabled = eANI_BOOLEAN_TRUE;
    }
}

/**
 * lim_pop_sap_deferred_msg() - pop deferred sap message
 *
 * @pmac: pointer to mac
 * @psessionentry: session of this entry
 *
 * This function is used to pop msg that in the deferred queue.
 *
 */
void
lim_pop_sap_deferred_msg(tpAniSirGlobal pmac, tpPESession sessionentry)
{
	struct slim_deferred_sap_msg* pdefermsg, *tmp;
	tpSirMacMgmtHdr mac_hdr;
	struct sap_offload_add_sta_req  *add_sta_req;
	tANI_U32 assoc_id;

	if (pmac == NULL || sessionentry == NULL )
		return;

	TAILQ_FOREACH_SAFE(pdefermsg, &pmac->lim.glim_sap_deferred_msgq.tq_head,
						list_elem, tmp) {

		add_sta_req = pdefermsg->deferredmsg.bodyptr;
		if (add_sta_req == NULL) {
			TAILQ_REMOVE(&pmac->lim.glim_sap_deferred_msgq.tq_head,
				pdefermsg, list_elem);
			limDeferMsg(pmac, &pdefermsg->deferredmsg);
			vos_mem_free(pdefermsg);
			continue;
		}
		assoc_id = add_sta_req->assoc_id;
		mac_hdr = (tpSirMacMgmtHdr)add_sta_req->conn_req;

		if (mac_hdr == NULL) {
			TAILQ_REMOVE(&pmac->lim.glim_sap_deferred_msgq.tq_head,
				pdefermsg, list_elem);
			limDeferMsg(pmac, &pdefermsg->deferredmsg);
			vos_mem_free(pdefermsg);
			continue;
		}
		if (!dph_entry_exist(pmac,
				mac_hdr->sa, assoc_id,
				&sessionentry->dph.dphHashTable)) {
			TAILQ_REMOVE(&pmac->lim.glim_sap_deferred_msgq.tq_head,
				pdefermsg, list_elem);

			limLog(pmac, LOGE, FL("pop def msg(H %pK T %pK)."
			"assid= %d,  %pM"),
			TAILQ_FIRST(&pmac->lim.glim_sap_deferred_msgq.tq_head),
			TAILQ_LAST(&pmac->lim.glim_sap_deferred_msgq.tq_head,
			t_slim_deferred_sap_msg_head),
			assoc_id, mac_hdr->sa);
			limDeferMsg(pmac, &pdefermsg->deferredmsg);
			vos_mem_free(pdefermsg);
		}
		limLog(pmac, LOGE, FL("msg not pop."
			"assid= %d,  %pM"), assoc_id, mac_hdr->sa);
	}
}

/**
 * lim_push_sap_deferred_msg() - push sap message into queue
 *
 * @pmac: pointer to mac
 * @lim_msgq: msg queue to store deferred msg
 *
 * This function is used to store msg into deferred queue.
 *
 */
void
lim_push_sap_deferred_msg(tpAniSirGlobal pmac, tpSirMsgQ lim_msgq)
{
	struct slim_deferred_sap_msg *pdefermsg;

	pdefermsg = vos_mem_malloc(sizeof(*pdefermsg));
	if (pdefermsg == NULL) {
		limLog(pmac, LOGE, FL("No mem for push msg %pK!"), lim_msgq);
		vos_mem_free(lim_msgq->bodyptr);
		return;
	}
	vos_mem_copy((tANI_U8 *)&pdefermsg->deferredmsg,
				(tANI_U8 *)lim_msgq,
				sizeof(tSirMsgQ));
	TAILQ_INSERT_TAIL(&pmac->lim.glim_sap_deferred_msgq.tq_head, pdefermsg,
		list_elem);

	limLog(pmac, LOGW, FL("push def msg(H %pK T %pK): P %pK."),
			TAILQ_FIRST(&pmac->lim.glim_sap_deferred_msgq.tq_head),
			TAILQ_LAST(&pmac->lim.glim_sap_deferred_msgq.tq_head,
			t_slim_deferred_sap_msg_head),
			pdefermsg);
}

/**
 * lim_init_sap_deferred_msg() - init sap deferred msg queue head
 *
 * @pmac: pointer to mac
 *
 * This function is used to int sap deferred msg queue head
 *
 */
void
lim_init_sap_deferred_msg_queue(tpAniSirGlobal pmac)
{
    TAILQ_INIT(&pmac->lim.glim_sap_deferred_msgq.tq_head);
}

/**
 * lim_cleanup_sap_deferred_msg() - cleanup sap deferred msg queue elements
 *
 * @pmac: pointer to mac
 *
 * This function is used to cleanup sap deferred msg queue elements
 *
 */
void
lim_cleanup_sap_deferred_msg_queue(tpAniSirGlobal pmac)
{
	struct slim_deferred_sap_msg *pdefermsg;
	tSirMsgQ *lim_msgq;

	while (!TAILQ_EMPTY(&pmac->lim.glim_sap_deferred_msgq.tq_head)) {
		pdefermsg = (struct slim_deferred_sap_msg*)TAILQ_FIRST(
				&pmac->lim.glim_sap_deferred_msgq.tq_head);
		lim_msgq = &pdefermsg->deferredmsg;
		vos_mem_free(lim_msgq->bodyptr);
		vos_mem_free(pdefermsg);
	}
}

void lim_sap_offload_add_sta(tpAniSirGlobal pmac, tpSirMsgQ lim_msgq)
{
    tpSirAssocReq assoc_req = NULL;
    tpDphHashNode sta_ds = NULL;
    tpSirMacMgmtHdr mac_hdr = NULL;
    struct sap_offload_add_sta_req  *add_sta_req = NULL;
    tpPESession session_entry = limIsApSessionActive(pmac);
    bool sta_inuse = false;

    add_sta_req = (struct sap_offload_add_sta_req *)lim_msgq->bodyptr;
    mac_hdr = (tpSirMacMgmtHdr)add_sta_req->conn_req;

    limLog(pmac, LOGW, FL("sta %pM aid %d"),
             mac_hdr->sa, add_sta_req->assoc_id);

    if (session_entry == NULL) {
        PELOGE(limLog(pmac, LOGE, FL(" Session not found"));)
        return;
    }
    assoc_req = vos_mem_malloc(sizeof(*assoc_req));
    if (NULL == assoc_req) {
        PELOGE(limLog(pmac, LOGE, FL("Allocate Memory failed in assoc_req"));)
        goto error;
    }
    vos_mem_set(assoc_req , sizeof(*assoc_req), 0);

    /* parse Assoc req frame for station information */
    sta_ds = _sap_offload_parse_assoc_req(pmac, assoc_req,
                                          add_sta_req, &sta_inuse);

    if (sta_inuse == true) {
        lim_push_sap_deferred_msg(pmac, lim_msgq);
        vos_mem_free(assoc_req );
        return;
    }

    if (sta_ds == NULL) {
        limSendDisassocMgmtFrame(pmac,
                            eSIR_MAC_UNSPEC_FAILURE_REASON,
                            mac_hdr->sa,
                            session_entry, FALSE);
        PELOGE(limLog(pmac, LOGE, FL("could not add hash entry."
            " disassoc sta %pM"),mac_hdr->sa);)
        vos_mem_free(assoc_req);
        goto error;
    }

    /* Parse Station Capability */
    _sap_offload_parse_sta_capability(sta_ds, assoc_req, add_sta_req);

    /* Parse Station HT/VHT information */
    if (_sap_offload_parse_sta_vht(pmac, sta_ds, assoc_req)
            == eSIR_FAILURE) {
        limSendDisassocMgmtFrame(pmac,
                            eSIR_MAC_UNSPEC_FAILURE_REASON,
                            mac_hdr->sa,
                            session_entry, FALSE);
        PELOGE(limLog(pmac, LOGE, FL("mismatch ht/vht information"
            " disassoc sta %pM"),mac_hdr->sa);)
        vos_mem_free(assoc_req);
        goto error;
    }

    /* Parse Station QOS information */
    _sap_offload_parse_sta_qos(pmac, sta_ds, assoc_req);

    if (assoc_req->ExtCap.present) {
        lim_set_stads_rtt_cap(sta_ds,
                (struct s_ext_cap *) assoc_req->ExtCap.bytes);
    } else {
        sta_ds->timingMeasCap = 0;
        PELOG1(limLog(pmac, LOG1, FL("ExtCap not present"));)
    }

    session_entry->parsedAssocReq[sta_ds->assocId] = assoc_req;

    if (limAddSta(pmac, sta_ds, false, session_entry) != eSIR_SUCCESS) {
        limLog(pmac, LOGE, FL("could not Add STA %pM with assocId=%d"),
                              mac_hdr->sa, sta_ds->assocId);
        limSendDisassocMgmtFrame(pmac,
                            eSIR_MAC_UNSPEC_FAILURE_REASON,
                            mac_hdr->sa,
                            session_entry, FALSE);
    }

error:
    vos_mem_free(add_sta_req);
    return;
}

void
lim_sap_offload_del_sta(tpAniSirGlobal pmac, tpSirMsgQ lim_msgq)
{
    struct sap_offload_del_sta_req *del_sta_req = NULL;
    tpDphHashNode sta_ds = NULL;
    tANI_U16 assoc_id = 0;
    tpPESession psession_entry = limIsApSessionActive(pmac);

    if (psession_entry == NULL) {
        PELOGE(limLog(pmac, LOGE, FL(" Session not found"));)
        return;
    }

    del_sta_req = ( struct sap_offload_del_sta_req *)lim_msgq->bodyptr;
    sta_ds = dphLookupHashEntry(pmac,
                                del_sta_req->sta_mac,
                                &assoc_id,
                                &psession_entry->dph.dphHashTable);
    limLog(pmac, LOGW, FL("sta %pM aid %d reason %x flag %x"),
                       del_sta_req->sta_mac, del_sta_req->assoc_id,
                       del_sta_req->reason_code,del_sta_req->flags);

    if (sta_ds == NULL) {
        /*
         * Disassociating STA is not associated.
         * Log error
         */
        PELOGE(limLog(pmac, LOGE,
           FL("received del sta event that sta not exist in table "
           "reasonCode=%d, addr "MAC_ADDRESS_STR),
           del_sta_req->reason_code,
           MAC_ADDR_ARRAY(del_sta_req->sta_mac));)
        goto error;
    }

    if (assoc_id != (tANI_U16)del_sta_req->assoc_id) {
        /*
         * Associate Id mismatch
         * Log error
         */
        PELOGE(limLog(pmac, LOGE,
           FL("received del sta event that sta assoc Id mismatch"));)
        goto error;
    }

    sta_ds->mlmStaContext.cleanupTrigger = eLIM_PEER_ENTITY_DISASSOC;

    if (SAP_OFL_DEL_STA_FLAG_RECONNECT == del_sta_req->flags) {
        sta_ds->mlmStaContext.disassocReason =
        (tSirMacReasonCodes)eSIR_SME_SAP_AUTH_OFFLOAD_PEER_UPDATE_STATUS;
    } else {
        sta_ds->mlmStaContext.disassocReason =
        (tSirMacReasonCodes)del_sta_req->reason_code;
    }
    sta_ds->mlmStaContext.updateContext = 1;

    limSendSmeDisassocInd(pmac, sta_ds, psession_entry);

error:
    vos_mem_free(del_sta_req);
    return;
}
#endif /* SAP_AUTH_OFFLOAD */

/**
 * lim_validate_received_frame_a1_addr() - To validate received frame's A1 addr
 * @mac_ctx: pointer to mac context
 * @a1: received frame's a1 address which is nothing but our self address
 * @session: PE session pointer
 *
 * This routine will validate, A1 addres of the received frame
 *
 * Return: true or false
 */
bool lim_validate_received_frame_a1_addr(tpAniSirGlobal mac_ctx,
		tSirMacAddr a1, tpPESession session)
{
	if (mac_ctx == NULL || session == NULL) {
		VOS_TRACE(VOS_MODULE_ID_PE, VOS_TRACE_LEVEL_INFO,
				"mac or session context is null");
		/* let main routine handle it */
		return true;
	}
	if (limIsGroupAddr(a1) || limIsAddrBC(a1)) {
		/* just for fail safe, don't handle MC/BC a1 in this routine */
		return true;
	}
	if (!vos_mem_compare(a1, session->selfMacAddr, 6)) {
		limLog(mac_ctx, LOGE,
			FL("Invalid A1 address in received frame"));
		return false;
	}
	return true;
}

/**
 * lim_set_stads_rtt_cap() - update station node RTT capability
 * @sta_ds: Station hash node
 * @ext_cap: Pointer to extended capability
 *
 * This funciton update hash node's RTT capability based on received
 * Extended capability IE.
 *
 * Return: None
 */
void lim_set_stads_rtt_cap(tpDphHashNode sta_ds, struct s_ext_cap *ext_cap)
{
	sta_ds->timingMeasCap = 0;
	sta_ds->timingMeasCap |= (ext_cap->timingMeas)?
				  RTT_TIMING_MEAS_CAPABILITY :
				  RTT_INVALID;
	sta_ds->timingMeasCap |= (ext_cap->fine_time_meas_initiator)?
				  RTT_FINE_TIME_MEAS_INITIATOR_CAPABILITY :
				  RTT_INVALID;
	sta_ds->timingMeasCap |= (ext_cap->fine_time_meas_responder)?
				  RTT_FINE_TIME_MEAS_RESPONDER_CAPABILITY :
				  RTT_INVALID;
}

/**
 * lim_check_and_reset_protection_params() - reset protection related parameters
 *
 * @mac_ctx: pointer to global mac structure
 *
 * resets protection related global parameters if the pe active session count
 * is zero.
 *
 * Return: None
 */
void lim_check_and_reset_protection_params(tpAniSirGlobal mac_ctx)
{
	if (!pe_get_active_session_count(mac_ctx)) {
		vos_mem_zero(&mac_ctx->lim.gLimOverlap11gParams,
			sizeof(mac_ctx->lim.gLimOverlap11gParams));
		vos_mem_zero(&mac_ctx->lim.gLimOverlap11aParams,
			sizeof(mac_ctx->lim.gLimOverlap11aParams));
		vos_mem_zero(&mac_ctx->lim.gLimOverlapHt20Params,
			sizeof(mac_ctx->lim.gLimOverlapHt20Params));
		vos_mem_zero(&mac_ctx->lim.gLimOverlapNonGfParams,
			sizeof(mac_ctx->lim.gLimOverlapNonGfParams));

		mac_ctx->lim.gHTOperMode = eSIR_HT_OP_MODE_PURE;
	}
}

/**
 * lim_send_ext_cap_ie() - send ext cap IE to FW
 * @mac_ctx: global MAC context
 * @session_entry: PE session
 * @extra_extcap: extracted ext cap
 * @merge: merge extra ext cap
 *
 * This function is invoked after VDEV is created to update firmware
 * about the extended capabilities that the corresponding VDEV is capable
 * of. Since STA/SAP can have different Extended capabilities set, this function
 * is called per vdev creation.
 *
 * Return: eHalStatus
 */
eHalStatus lim_send_ext_cap_ie(tpAniSirGlobal mac_ctx,
			       uint32_t session_id,
			       tDot11fIEExtCap *extra_extcap, bool merge)
{
	tDot11fIEExtCap ext_cap_data = {0};
	uint32_t dot11mode, num_bytes;
	bool vht_enabled = false;
	struct vdev_ie_info *vdev_ie;
	vos_msg_t msg = {0};
	tSirRetStatus status;
	uint8_t *temp, i;

	wlan_cfgGetInt(mac_ctx, WNI_CFG_DOT11_MODE, &dot11mode);
	if (IS_DOT11_MODE_VHT(dot11mode))
		vht_enabled = true;

	status = PopulateDot11fExtCap(mac_ctx, vht_enabled, &ext_cap_data,
				      NULL);
	if (eSIR_SUCCESS != status) {
		limLog(mac_ctx, LOGE, FL("Failed to populate ext cap IE"));
		return eHAL_STATUS_FAILURE;
	}
	num_bytes = ext_cap_data.num_bytes;

	if (merge && NULL != extra_extcap && extra_extcap->num_bytes > 0) {
		if (extra_extcap->num_bytes > ext_cap_data.num_bytes)
			num_bytes = extra_extcap->num_bytes;
		lim_merge_extcap_struct(&ext_cap_data, extra_extcap, true);
	}

	/* Allocate memory for the WMI request, and copy the parameter */
	vdev_ie = vos_mem_malloc(sizeof(*vdev_ie) + num_bytes);
	if (!vdev_ie) {
		limLog(mac_ctx, LOGE, FL("Failed to allocate memory"));
		return eHAL_STATUS_FAILED_ALLOC;
	}

	vdev_ie->vdev_id = session_id;
	vdev_ie->ie_id = DOT11F_EID_EXTCAP;
	vdev_ie->length = num_bytes;

	limLog(mac_ctx, LOG1, FL("vdev %d ieid %d len %d"), session_id,
			DOT11F_EID_EXTCAP, num_bytes);
	temp = ext_cap_data.bytes;
	for (i=0; i < num_bytes; i++, temp++)
		limLog(mac_ctx, LOG2, FL("%d byte is %02x"), i+1, *temp);

	vdev_ie->data = (uint8_t *)vdev_ie + sizeof(*vdev_ie);
	vos_mem_copy(vdev_ie->data, ext_cap_data.bytes, num_bytes);

	msg.type = WDA_SET_IE_INFO;
	msg.bodyptr = vdev_ie;
	msg.reserved = 0;

	if (VOS_STATUS_SUCCESS !=
		vos_mq_post_message(VOS_MODULE_ID_WDA, &msg)) {
		limLog(mac_ctx, LOGE,
		       FL("Not able to post WDA_SET_IE_INFO to WDA"));
		vos_mem_free(vdev_ie);
		return eHAL_STATUS_FAILURE;
	}

	return eHAL_STATUS_SUCCESS;
}

/**
 * lim_strip_extcap_ie() - strip extended capability IE from IE buffer
 * @mac_ctx: global MAC context
 * @addn_ie: Additional IE buffer
 * @addn_ielen: Length of additional IE
 * @extracted_ie: if not NULL, copy the stripped IE to this buffer
 *
 * This utility function is used to strip of the extended capability IE present
 * in additional IE buffer.
 *
 * Return: tSirRetStatus
 */
tSirRetStatus lim_strip_extcap_ie(tpAniSirGlobal mac_ctx,
		uint8_t *addn_ie, uint16_t *addn_ielen, uint8_t *extracted_ie)
{
	uint8_t* tempbuf = NULL;
	uint16_t templen = 0;
	int left = *addn_ielen;
	uint8_t *ptr = addn_ie;
	uint8_t elem_id, elem_len;

	if (NULL == addn_ie) {
		limLog(mac_ctx, LOG1, FL("NULL addn_ie pointer"));
		return eSIR_IGNORE_IE ;
	}

	tempbuf = vos_mem_malloc(left);
	if (NULL == tempbuf) {
		limLog(mac_ctx, LOGE, FL("Unable to allocate memory"));
		return eSIR_MEM_ALLOC_FAILED;
	}

	while(left >= 2) {
		elem_id  = ptr[0];
		elem_len = ptr[1];
		left -= 2;
		if (elem_len > left) {
			limLog( mac_ctx, LOGE,
				FL("Invalid IEs eid = %d elem_len=%d left=%d"),
				elem_id, elem_len, left);
			vos_mem_free(tempbuf);
			return eSIR_FAILURE;
		}
		if (!(DOT11F_EID_EXTCAP == elem_id)) {
			vos_mem_copy (tempbuf + templen, &ptr[0], elem_len + 2);
			templen += (elem_len + 2);
		} else {
			if (NULL != extracted_ie) {
				vos_mem_set(extracted_ie,
					DOT11F_IE_EXTCAP_MAX_LEN + 2, 0);
				if (elem_len <= DOT11F_IE_EXTCAP_MAX_LEN)
					vos_mem_copy(extracted_ie, &ptr[0],
						     elem_len + 2);
			}
		}
		left -= elem_len;
		ptr += (elem_len + 2);
	}
	vos_mem_copy (addn_ie, tempbuf, templen);

	*addn_ielen = templen;
	vos_mem_free(tempbuf);

	return eSIR_SUCCESS;
}

/**
 * lim_update_extcap_struct() - poputlate the dot11f structure
 * @mac_ctx: global MAC context
 * @buf: extracted IE buffer
 * @dst: extended capability IE structure to be updated
 *
 * This function is used to update the extended capability structure
 * with @buf.
 *
 * Return: None
 */
void lim_update_extcap_struct(tpAniSirGlobal mac_ctx,
	uint8_t *buf, tDot11fIEExtCap *dst)
{
	uint8_t out[DOT11F_IE_EXTCAP_MAX_LEN];

	if (NULL == buf) {
		limLog( mac_ctx, LOGE, FL("Invalid Buffer Address"));
		return;
	}

	if(NULL == dst) {
		limLog(mac_ctx, LOGE, FL("NULL dst pointer"));
		return ;
	}

	if (DOT11F_EID_EXTCAP != buf[0] || buf[1] > DOT11F_IE_EXTCAP_MAX_LEN) {
		limLog(mac_ctx, LOG1, FL("Invalid IEs eid = %d elem_len=%d "),
				buf[0],buf[1]);
		return;
	}

	vos_mem_set((uint8_t *)&out[0], DOT11F_IE_EXTCAP_MAX_LEN, 0);
	vos_mem_copy(&out[0], &buf[2], buf[1]);

	if (DOT11F_PARSE_SUCCESS != dot11fUnpackIeExtCap(mac_ctx, &out[0],
                                        buf[1], dst))
		limLog(mac_ctx, LOGE, FL("dot11fUnpackIeExtCap Parse Error "));
}

/**
 * lim_strip_extcap_update_struct - strip extended capability IE and populate
 *                                  the dot11f structure
 * @mac_ctx: global MAC context
 * @addn_ie: Additional IE buffer
 * @addn_ielen: Length of additional IE
 * @dst: extended capability IE structure to be updated
 *
 * This function is used to strip extended capability IE from IE buffer and
 * update the passed structure.
 *
 * Return: tSirRetStatus
 */
tSirRetStatus lim_strip_extcap_update_struct(tpAniSirGlobal mac_ctx,
		uint8_t* addn_ie, uint16_t *addn_ielen, tDot11fIEExtCap *dst)
{
	uint8_t extracted_buff[DOT11F_IE_EXTCAP_MAX_LEN + 2];
	tSirRetStatus status;

	vos_mem_set((uint8_t* )&extracted_buff[0], DOT11F_IE_EXTCAP_MAX_LEN + 2,
		     0);
	status = lim_strip_extcap_ie(mac_ctx, addn_ie, addn_ielen,
				     extracted_buff);
	if (eSIR_SUCCESS != status) {
		limLog(mac_ctx, LOG1,
		       FL("Failed to strip extcap IE status = (%d)."), status);
		return status;
	}

	/* update the extracted ExtCap to struct*/
	lim_update_extcap_struct(mac_ctx, extracted_buff, dst);
	return status;
}

/**
 * lim_merge_extcap_struct() - merge extended capabilities info
 * @dst: destination extended capabilities
 * @src: source extended capabilities
 * @add: true if add the capabilites, false if strip the capabilites.
 *
 * This function is used to take @src info and add/strip it to/from
 * @dst extended capabilities info.
 *
 * Return: None
 */
void lim_merge_extcap_struct(tDot11fIEExtCap *dst,
			     tDot11fIEExtCap *src,
			     bool add)
{
	uint8_t *tempdst = (uint8_t *)dst->bytes;
	uint8_t *tempsrc = (uint8_t *)src->bytes;
	uint8_t structlen = member_size(tDot11fIEExtCap, bytes);

	/* Return if @src not present */
	if (!src->present)
		return;

	/* Return if strip the capabilites from @dst which not present */
	if (!dst->present && !add)
		return;

	/* Merge the capabilites info in other cases */
	while (tempdst && tempsrc && structlen--) {
		if (add)
			*tempdst |= *tempsrc;
		else
			*tempdst &= *tempsrc;
		tempdst++;
		tempsrc++;
	}
	dst->num_bytes = lim_compute_ext_cap_ie_length(dst);
        if (dst->num_bytes == 0)
		dst->present = 0;
	else
		dst->present = 1;
}

/**
 * lim_get_80Mhz_center_channel - finds 80 Mhz center channel
 *
 * @primary_channel:   Primary channel for given 80 MHz band
 *
 * There are fixed 80MHz band and for each fixed band there is only one center
 * valid channel. Also location of primary channel decides what 80 MHz band will
 * it use, hence it decides what center channel will be used. This function
 * does thus calculation and returns the center channel.
 *
 * Return: center channel
 */
uint8_t
lim_get_80Mhz_center_channel(uint8_t primary_channel)
{
	if(primary_channel >= 36 && primary_channel <= 48)
		return (36+48)/2;
	if(primary_channel >= 52 && primary_channel <= 64)
		return (52+64)/2;
	if(primary_channel >= 100 && primary_channel <= 112)
		return (100+112)/2;
	if(primary_channel >= 116 && primary_channel <= 128)
		return (116+128)/2;
	if(primary_channel >= 132 && primary_channel <= 144)
		return (132+144)/2;
	if(primary_channel >= 149 && primary_channel <= 161)
		return (149+161)/2;

	return HAL_INVALID_CHANNEL_ID;
}

/**
 * lim_compute_ext_cap_ie_length - compute the length of ext cap ie
 * based on the bits set
 * @ext_cap: extended IEs structure
 *
 * Return: length of the ext cap ie, 0 means should not present
 */
tANI_U8 lim_compute_ext_cap_ie_length (tDot11fIEExtCap *ext_cap) {
	tANI_U8 i = DOT11F_IE_EXTCAP_MAX_LEN;

	while (i) {
		if (ext_cap->bytes[i-1]) {
			break;
		}
		i --;
	}

	return i;
}

/**
 * lim_is_robust_mgmt_action_frame() - Check if action catagory is
 * robust action frame
 * @action_catagory: Action frame catagory.
 *
 * This function is used to check if given action catagory is robust
 * action frame.
 *
 * Return: bool
 */
bool lim_is_robust_mgmt_action_frame(uint8_t action_catagory)
{
	switch (action_catagory) {
		/*
		 * NOTE: This function doesn't take care of the DMG
		 * (Directional Multi-Gigatbit) BSS case as 8011ad
		 * support is not yet added. In future, if the support
		 * is required then this function need few more arguments
		 * and little change in logic.
		 */
		case SIR_MAC_ACTION_SPECTRUM_MGMT:
		case SIR_MAC_ACTION_QOS_MGMT:
		case SIR_MAC_ACTION_DLP:
		case SIR_MAC_ACTION_BLKACK:
		case SIR_MAC_ACTION_RRM:
		case SIR_MAC_ACTION_FAST_BSS_TRNST:
		case SIR_MAC_ACTION_SA_QUERY:
		case SIR_MAC_ACTION_PROT_DUAL_PUB:
		case SIR_MAC_ACTION_WNM:
		case SIR_MAC_ACITON_MESH:
		case SIR_MAC_ACTION_MHF:
		case SIR_MAC_ACTION_FST:
			return true;
		default:
			VOS_TRACE(VOS_MODULE_ID_PE, VOS_TRACE_LEVEL_INFO,
				FL("non-PMF action category[%d] "),
				action_catagory);
		break;
	}
	return false;
}

/**
 * lim_update_caps_info_for_bss - Update capability info for this BSS
 *
 * @mac_ctx: mac context
 * @caps: Pointer to capability info to be updated
 * @bss_caps: Capability info of the BSS
 *
 * Update the capability info in Assoc/Reassoc request frames and reset
 * the spectrum management, short preamble, immediate block ack bits
 * if the BSS doesnot support it
 *
 * Return: None
 */
void lim_update_caps_info_for_bss(tpAniSirGlobal mac_ctx,
					uint16_t *caps, uint16_t bss_caps)
{
	if (!(bss_caps & LIM_SPECTRUM_MANAGEMENT_BIT_MASK)) {
		*caps &= (~LIM_SPECTRUM_MANAGEMENT_BIT_MASK);
		limLog(mac_ctx, LOG1, FL("Clearing spectrum management:no AP support"));
	}

	if (!(bss_caps & LIM_SHORT_PREAMBLE_BIT_MASK)) {
		*caps &= (~LIM_SHORT_PREAMBLE_BIT_MASK);
		limLog(mac_ctx, LOG1, FL("Clearing short preamble:no AP support"));
	}

	if (!(bss_caps & LIM_IMMEDIATE_BLOCK_ACK_MASK)) {
		*caps &= (~LIM_IMMEDIATE_BLOCK_ACK_MASK);
		limLog(mac_ctx, LOG1, FL("Clearing Immed Blk Ack:no AP support"));
	}
}

/*
 * lim_parse_beacon_for_tim() - Extract TIM and beacon timestamp
 * from beacon frame.
 * @mac_ctx: mac context
 * @rx_packet_info: beacon frame
 * @session: Session on which beacon is received
 *
 * This function is used if beacon is corrupted and parser API fails to
 * parse the whole beacon. Try to extract the TIM params and timestamp
 * from the beacon, required to enter BMPS
 *
 * Return: void
 */
void lim_parse_beacon_for_tim(tpAniSirGlobal mac_ctx,
	uint8_t* rx_packet_info, tpPESession session)
{
	uint32_t frame_len;
	uint8_t *frame;
	uint8_t *ie_ptr;
	tSirMacTim *tim;

	frame = WDA_GET_RX_MPDU_DATA(rx_packet_info);
	frame_len = WDA_GET_RX_PAYLOAD_LEN(rx_packet_info);

	if (frame_len < (SIR_MAC_B_PR_SSID_OFFSET + SIR_MAC_MIN_IE_LEN)) {
		limLog(mac_ctx, LOGE, FL("Beacon length too short to parse"));
		return;
	}

	ie_ptr = lim_get_ie_ptr((frame + SIR_MAC_B_PR_SSID_OFFSET),
				frame_len, SIR_MAC_TIM_EID);

	if (NULL != ie_ptr) {
		/* Ignore EID and Length field */
		tim = (tSirMacTim *)(ie_ptr + IE_LEN_SIZE + IE_EID_SIZE);

		vos_mem_copy((uint8_t*)&session->lastBeaconTimeStamp,
				(uint8_t*)frame, sizeof(uint64_t));
		if (tim->dtimCount >= MAX_DTIM_COUNT)
			tim->dtimCount = DTIM_COUNT_DEFAULT;
		if (tim->dtimPeriod >= MAX_DTIM_PERIOD)
			tim->dtimPeriod = DTIM_PERIOD_DEFAULT;
		session->lastBeaconDtimCount = tim->dtimCount;
		session->lastBeaconDtimPeriod = tim->dtimPeriod;
		session->currentBssBeaconCnt++;

		limLog(mac_ctx, LOG1,
			FL("currentBssBeaconCnt %d lastBeaconDtimCount %d lastBeaconDtimPeriod %d"),
			session->currentBssBeaconCnt,
			session->lastBeaconDtimCount,
			session->lastBeaconDtimPeriod);

	}
	return;
}

bool lim_check_if_vendor_oui_match(tpAniSirGlobal mac_ctx,
                    uint8_t *oui, uint8_t oui_len,
                   uint8_t *ie, uint8_t ie_len)
{
    uint8_t *ptr = ie;
    uint8_t elem_id = 0;

    if (NULL == ie || 0 == ie_len) {
        limLog(mac_ctx, LOG1, FL("IE Null or ie len zero %d"), ie_len);
        return false;
    }

    elem_id = *ie;

    if (elem_id == IE_EID_VENDOR &&
        !adf_os_mem_cmp(&ptr[2], oui, oui_len))
        return true;
    else
        return false;
}

void lim_decrement_pending_mgmt_count(tpAniSirGlobal mac_ctx)
{
       adf_os_spin_lock(&mac_ctx->sys.bbt_mgmt_lock);
       if (!mac_ctx->sys.sys_bbt_pending_mgmt_count) {
               adf_os_spin_unlock(&mac_ctx->sys.bbt_mgmt_lock);
               limLog(mac_ctx, LOGW,
                       FL("sys_bbt_pending_mgmt_count value is 0"));
               return;
       }
       mac_ctx->sys.sys_bbt_pending_mgmt_count--;
       adf_os_spin_unlock(&mac_ctx->sys.bbt_mgmt_lock);
}
