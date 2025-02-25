/*
 * Copyright (c) 2012-2020 The Linux Foundation. All rights reserved.
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

/**========================================================================

  \file  wlan_hdd_assoc.c
  \brief WLAN Host Device Driver implementation

  ========================================================================*/
/**=========================================================================
                       EDIT HISTORY FOR FILE


  This section contains comments describing changes made to the module.
  Notice that changes are listed in reverse chronological order.


  $Header:$   $DateTime: $ $Author: $


  when        who    what, where, why
  --------    ---    --------------------------------------------------------
  05/06/09     Shailender     Created module.
  ==========================================================================*/

#include "wlan_hdd_includes.h"
#include <aniGlobal.h>
#include "dot11f.h"
#include "wlan_nlink_common.h"
#include "wlan_hdd_power.h"
#include "wlan_hdd_trace.h"
#include <linux/ieee80211.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include "wlan_hdd_cfg80211.h"
#include "csrInsideApi.h"
#include "wlan_hdd_p2p.h"
#ifdef FEATURE_WLAN_TDLS
#include "wlan_hdd_tdls.h"
#endif
#include "sme_Api.h"
#include "wlan_hdd_hostapd.h"
#ifdef IPA_OFFLOAD
#include <wlan_hdd_ipa.h>
#endif
#include <vos_sched.h>
#include <wlan_logging_sock_svc.h>
#include "tl_shim.h"
#include "wlan_hdd_oemdata.h"
#include "wlan_hdd_tsf.h"

#include "adf_trace.h"

#ifdef WLAN_FEATURE_SAP_TO_FOLLOW_STA_CHAN
#include "wlan_hdd_hostapd.h"
#endif//#ifdef WLAN_FEATURE_SAP_TO_FOLLOW_STA_CHAN

struct ether_addr
{
    u_char  ether_addr_octet[6];
};
// These are needed to recognize WPA and RSN suite types
#define HDD_WPA_OUI_SIZE 4
v_U8_t ccpWpaOui00[ HDD_WPA_OUI_SIZE ] = { 0x00, 0x50, 0xf2, 0x00 };
v_U8_t ccpWpaOui01[ HDD_WPA_OUI_SIZE ] = { 0x00, 0x50, 0xf2, 0x01 };
v_U8_t ccpWpaOui02[ HDD_WPA_OUI_SIZE ] = { 0x00, 0x50, 0xf2, 0x02 };
v_U8_t ccpWpaOui03[ HDD_WPA_OUI_SIZE ] = { 0x00, 0x50, 0xf2, 0x03 };
v_U8_t ccpWpaOui04[ HDD_WPA_OUI_SIZE ] = { 0x00, 0x50, 0xf2, 0x04 };
v_U8_t ccpWpaOui05[ HDD_WPA_OUI_SIZE ] = { 0x00, 0x50, 0xf2, 0x05 };
#ifdef FEATURE_WLAN_ESE
v_U8_t ccpWpaOui06[ HDD_WPA_OUI_SIZE ] = { 0x00, 0x40, 0x96, 0x00 }; // CCKM
#endif /* FEATURE_WLAN_ESE */
#define HDD_RSN_OUI_SIZE 4
v_U8_t ccpRSNOui00[ HDD_RSN_OUI_SIZE ] = { 0x00, 0x0F, 0xAC, 0x00 }; // group cipher
v_U8_t ccpRSNOui01[ HDD_RSN_OUI_SIZE ] = { 0x00, 0x0F, 0xAC, 0x01 }; // WEP-40 or RSN
v_U8_t ccpRSNOui02[ HDD_RSN_OUI_SIZE ] = { 0x00, 0x0F, 0xAC, 0x02 }; // TKIP or RSN-PSK
v_U8_t ccpRSNOui03[ HDD_RSN_OUI_SIZE ] = { 0x00, 0x0F, 0xAC, 0x03 }; // Reserved
v_U8_t ccpRSNOui04[ HDD_RSN_OUI_SIZE ] = { 0x00, 0x0F, 0xAC, 0x04 }; // AES-CCMP
v_U8_t ccpRSNOui05[ HDD_RSN_OUI_SIZE ] = { 0x00, 0x0F, 0xAC, 0x05 }; // WEP-104
#ifdef FEATURE_WLAN_ESE
v_U8_t ccpRSNOui06[ HDD_RSN_OUI_SIZE ] = { 0x00, 0x40, 0x96, 0x00 }; // CCKM
#endif /* FEATURE_WLAN_ESE */
#ifdef WLAN_FEATURE_11W
v_U8_t ccpRSNOui07[ HDD_RSN_OUI_SIZE ] = { 0x00, 0x0F, 0xAC, 0x06 }; // RSN-PSK-SHA256
/* RSN-8021X-SHA256 */
v_U8_t ccpRSNOui08[ HDD_RSN_OUI_SIZE ] = { 0x00, 0x0F, 0xAC, 0x05 };
#endif

/* OWE https://tools.ietf.org/html/rfc8110 */
uint8_t ccp_rsn_oui_18[HDD_RSN_OUI_SIZE] = {0x00, 0x0F, 0xAC, 0x12};

#ifdef WLAN_FEATURE_FILS_SK
uint8_t ccp_rsn_oui_0e[HDD_RSN_OUI_SIZE] = {0x00, 0x0F, 0xAC, 0x0E};
uint8_t ccp_rsn_oui_0f[HDD_RSN_OUI_SIZE] = {0x00, 0x0F, 0xAC, 0x0F};
uint8_t ccp_rsn_oui_10[HDD_RSN_OUI_SIZE] = {0x00, 0x0F, 0xAC, 0x10};
uint8_t ccp_rsn_oui_11[HDD_RSN_OUI_SIZE] = {0x00, 0x0F, 0xAC, 0x11};
#endif

#ifdef WLAN_FEATURE_SAE
uint8_t ccp_rsn_oui_80[HDD_RSN_OUI_SIZE] = {0x00, 0x0F, 0xAC, 0x08};
uint8_t ccp_rsn_oui_90[HDD_RSN_OUI_SIZE] = {0x00, 0x0F, 0xAC, 0x09};
#endif

#if defined(WLAN_FEATURE_VOWIFI_11R)
// Offset where the EID-Len-IE, start.
#define FT_ASSOC_RSP_IES_OFFSET 6 /* Capability(2) + AID(2) + Status Code(2)*/
#define FT_ASSOC_REQ_IES_OFFSET 4 /* Capability(2) + LI(2) */
#endif

#define BEACON_FRAME_IES_OFFSET 12

#define NUM_BITS_IN_INT 32
static const int beacon_filter_table[] = {
	SIR_MAC_DS_PARAM_SET_EID,
	SIR_MAC_ERP_INFO_EID,
	SIR_MAC_EDCA_PARAM_SET_EID,
	SIR_MAC_QOS_CAPABILITY_EID,
	SIR_MAC_HT_INFO_EID,
#ifdef WLAN_FEATURE_11AC
	SIR_MAC_VHT_OPMODE_EID,
	SIR_MAC_VHT_OPERATION_EID,
#endif
};

static eHalStatus hdd_RoamSetKeyCompleteHandler( hdd_adapter_t *pAdapter,
                                                tCsrRoamInfo *pRoamInfo,
                                                tANI_U32 roamId,
                                                eRoamCmdStatus roamStatus,
                                                eCsrRoamResult roamResult );

#if defined(WLAN_FEATURE_SAE) && \
	defined(CFG80211_EXTERNAL_AUTH_SUPPORT)
/**
 * wlan_hdd_sae_callback() - Sends SAE info to supplicant
 * @adapter: pointer adapter context
 * @roam_info: pointer to roam info
 *
 * This API is used to send required SAE info to trigger SAE in supplicant.
 *
 * Return: None
 */
static void wlan_hdd_sae_callback(hdd_adapter_t *adapter,
				tCsrRoamInfo *roam_info)
{
	hdd_context_t *hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	int flags;
	struct sir_sae_info *sae_info = roam_info->sae_info;
	struct cfg80211_external_auth_params params = {0};

	if (wlan_hdd_validate_context(hdd_ctx))
		return;

	if (!sae_info) {
		hddLog(LOGE, FL("SAE info in NULL"));
		return;
	}

	flags = vos_get_gfp_flags();

	params.key_mgmt_suite = 0x00;
	params.key_mgmt_suite |= 0x0F << 8;
	params.key_mgmt_suite |= 0xAC << 16;
	params.key_mgmt_suite |= 0x8 << 24;

	params.action = NL80211_EXTERNAL_AUTH_START;
	vos_mem_copy(params.bssid, sae_info->peer_mac_addr.bytes,
		VOS_MAC_ADDR_SIZE);
	vos_mem_copy(params.ssid.ssid, sae_info->ssid.ssId,
		sae_info->ssid.length);
	params.ssid.ssid_len = sae_info->ssid.length;

	cfg80211_external_auth_request(adapter->dev, &params, flags);
	hddLog(LOG1, FL("SAE: sent cmd"));
}
#else
static void wlan_hdd_sae_callback(hdd_adapter_t *adapter,
				tCsrRoamInfo *roam_info)
{ }
#endif

static v_VOID_t
hdd_connSetAuthenticated(hdd_adapter_t *pAdapter, v_U8_t authState)
{
   hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
   hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);

   /* save the new connection state */
   hddLog(LOG1, FL("Authenticated state Changed from oldState:%d to State:%d"),
                    pHddStaCtx->conn_info.uIsAuthenticated, authState);
   pHddStaCtx->conn_info.uIsAuthenticated = authState;

   /* Check is pending ROC request or not when auth state changed */
   queue_delayed_work(system_freezable_power_efficient_wq,
                      &pHddCtx->rocReqWork, 0);
}

v_VOID_t hdd_connSetConnectionState( hdd_adapter_t *pAdapter,
                                                eConnectionState connState )
{
   hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
   hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);

   /* save the new connection state */
   hddLog(LOG1, FL("%pS Changed connectionState from oldState:%d to State:%d"),
                    (void *)_RET_IP_,
                    pHddStaCtx->conn_info.connState, connState);

   hdd_tsf_notify_wlan_state_change(pAdapter,
		   pHddStaCtx->conn_info.connState, connState);
   pHddStaCtx->conn_info.connState = connState;

   /* Check is pending ROC request or not when connection state changed */
   queue_delayed_work(system_freezable_power_efficient_wq,
                      &pHddCtx->rocReqWork, 0);
}

// returns FALSE if not connected.
// returns TRUE for the two 'connected' states (Infra Associated or IBSS Connected ).
// returns the connection state.  Can specify NULL if you dont' want to get the actual state.

static inline v_BOOL_t hdd_connGetConnectionState( hdd_station_ctx_t *pHddStaCtx,
                                    eConnectionState *pConnState )
{
   v_BOOL_t fConnected;
   eConnectionState connState;

   // get the connection state.
   connState = pHddStaCtx->conn_info.connState;
   // Set the fConnected return variable based on the Connected State.
   if ( eConnectionState_Associated == connState ||
        eConnectionState_IbssConnected == connState ||
        eConnectionState_IbssDisconnected == connState)
   {
      fConnected = VOS_TRUE;
   }
   else
   {
      fConnected = VOS_FALSE;
   }

   if ( pConnState )
   {
      *pConnState = connState;
   }

   return( fConnected );
}

v_BOOL_t hdd_connIsConnected( hdd_station_ctx_t *pHddStaCtx )
{
   return( hdd_connGetConnectionState( pHddStaCtx, NULL ) );
}

bool hdd_is_connecting(hdd_station_ctx_t *hdd_sta_ctx)
{
	return (hdd_sta_ctx->conn_info.connState ==
		eConnectionState_Connecting);
}

eCsrBand hdd_connGetConnectedBand( hdd_station_ctx_t *pHddStaCtx )
{
   v_U8_t staChannel = 0;

   if ( eConnectionState_Associated == pHddStaCtx->conn_info.connState )
   {
       staChannel = pHddStaCtx->conn_info.operationChannel;
   }

   if ( staChannel > 0 && staChannel < 14 )
       return eCSR_BAND_24;
   else if (staChannel >= 36 && staChannel <= 184 )
      return eCSR_BAND_5G;
   else  /* If station is not connected return as eCSR_BAND_ALL */
      return eCSR_BAND_ALL;
}

static inline v_BOOL_t hdd_connGetConnectedCipherAlgo( hdd_station_ctx_t *pHddStaCtx, eCsrEncryptionType *pConnectedCipherAlgo )
{
    v_BOOL_t fConnected = VOS_FALSE;

    fConnected = hdd_connGetConnectionState( pHddStaCtx, NULL );

    if ( pConnectedCipherAlgo )
    {
        *pConnectedCipherAlgo = pHddStaCtx->conn_info.ucEncryptionType;
    }

    return( fConnected );
}

inline v_BOOL_t hdd_connGetConnectedBssType( hdd_station_ctx_t *pHddStaCtx, eMib_dot11DesiredBssType *pConnectedBssType )
{
    v_BOOL_t fConnected = VOS_FALSE;

    fConnected = hdd_connGetConnectionState( pHddStaCtx, NULL );

    if ( pConnectedBssType )
    {
        *pConnectedBssType = pHddStaCtx->conn_info.connDot11DesiredBssType;
    }

    return( fConnected );
}

static inline void hdd_connSaveConnectedBssType( hdd_station_ctx_t *pHddStaCtx, eCsrRoamBssType csrRoamBssType )
{
   switch( csrRoamBssType )
   {
      case eCSR_BSS_TYPE_INFRASTRUCTURE:
          pHddStaCtx->conn_info.connDot11DesiredBssType = eMib_dot11DesiredBssType_infrastructure;
         break;

      case eCSR_BSS_TYPE_IBSS:
      case eCSR_BSS_TYPE_START_IBSS:
          pHddStaCtx->conn_info.connDot11DesiredBssType = eMib_dot11DesiredBssType_independent;
         break;

      /** We will never set the BssType to 'any' when attempting a connection
            so CSR should never send this back to us.*/
      case eCSR_BSS_TYPE_ANY:
      default:
         VOS_ASSERT( 0 );
         break;
   }

}

/**
 * hdd_unset_beacon_filter() - remove beacon filter
 * @adapter: Pointer to the hdd adapter
 *
 * Return: 0 on success and errno on failure
 */
static int hdd_unset_beacon_filter(hdd_adapter_t *adapter)
{
	VOS_STATUS vos_status = VOS_STATUS_E_FAILURE;

	vos_status = sme_unset_beacon_filter(adapter->sessionId);
	if (!VOS_IS_STATUS_SUCCESS(vos_status))
		return -EFAULT;

	return 0;
}

/**
 * hdd_set_beacon_filter() - set beacon filter
 * @adapter: Pointer to the hdd adapter
 *
 * Return: 0 on success and errno on failure
 */
static int hdd_set_beacon_filter(hdd_adapter_t *adapter)
{
	int i;
	uint32_t ie_map[8] = {0};
	VOS_STATUS vos_status = VOS_STATUS_E_FAILURE;
	tHalHandle hal_ptr = WLAN_HDD_GET_HAL_CTX(adapter);
	tpAniSirGlobal mac_ptr = PMAC_STRUCT(hal_ptr);

	for (i = 0; i < ARRAY_SIZE(beacon_filter_table); i++)
		__set_bit(beacon_filter_table[i],
			  (unsigned long int *)ie_map);


	if (TRUE == mac_ptr->sta_change_cc_via_beacon &&
	    adapter->device_mode == WLAN_HDD_INFRA_STATION)
		__set_bit(SIR_MAC_COUNTRY_EID, (unsigned long int *)ie_map);

	vos_status = sme_set_beacon_filter(adapter->sessionId, ie_map);
	if (!VOS_IS_STATUS_SUCCESS(vos_status)) {
		hddLog(LOGE, "%s: failed to set beacon filter",
			 __func__);
		return -EFAULT;
	}
	return 0;
}

/**
 * hdd_copy_vht_caps()- copy vht caps info from roam info to
 *  hdd station context.
 * @hdd_sta_ctx: pointer to hdd station context
 * @roam_info: pointer to roam info
 *
 * Return: None
 */
static void hdd_copy_ht_caps(hdd_station_ctx_t *hdd_sta_ctx,
				     tCsrRoamInfo *roam_info)
{
	tDot11fIEHTCaps *roam_ht_cap = &roam_info->ht_caps;
	struct ieee80211_ht_cap *hdd_ht_cap = &hdd_sta_ctx->conn_info.ht_caps;
	uint32_t i, temp_ht_cap;

	adf_os_mem_zero(hdd_ht_cap, sizeof(struct ieee80211_ht_cap));

	if (roam_ht_cap->advCodingCap)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_LDPC_CODING;
	if (roam_ht_cap->supportedChannelWidthSet)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	temp_ht_cap = roam_ht_cap->mimoPowerSave &
	    (IEEE80211_HT_CAP_SM_PS >> IEEE80211_HT_CAP_SM_PS_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->cap_info |=
			temp_ht_cap << IEEE80211_HT_CAP_SM_PS_SHIFT;
	if (roam_ht_cap->greenField)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_GRN_FLD;
	if (roam_ht_cap->shortGI20MHz)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_SGI_20;
	if (roam_ht_cap->shortGI40MHz)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_SGI_40;
	if (roam_ht_cap->txSTBC)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_TX_STBC;
	temp_ht_cap = roam_ht_cap->rxSTBC & (IEEE80211_HT_CAP_RX_STBC >>
	    IEEE80211_HT_CAP_RX_STBC_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->cap_info |=
			temp_ht_cap << IEEE80211_HT_CAP_RX_STBC_SHIFT;
	if (roam_ht_cap->delayedBA)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_DELAY_BA;
	if (roam_ht_cap->maximalAMSDUsize)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_MAX_AMSDU;
	if (roam_ht_cap->dsssCckMode40MHz)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_DSSSCCK40;
	if (roam_ht_cap->psmp)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_RESERVED;
	if (roam_ht_cap->stbcControlFrame)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_40MHZ_INTOLERANT;
	if (roam_ht_cap->lsigTXOPProtection)
		hdd_ht_cap->cap_info |= IEEE80211_HT_CAP_LSIG_TXOP_PROT;

	/* 802.11n HT capability AMPDU settings (for ampdu_params_info) */
	if (roam_ht_cap->maxRxAMPDUFactor)
		hdd_ht_cap->ampdu_params_info |=
			IEEE80211_HT_AMPDU_PARM_FACTOR;
	temp_ht_cap = roam_ht_cap->mpduDensity &
	    (IEEE80211_HT_AMPDU_PARM_DENSITY >>
	     IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->ampdu_params_info |=
		temp_ht_cap << IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT;

	/* 802.11n HT extended capabilities masks */
	if (roam_ht_cap->pco)
		hdd_ht_cap->extended_ht_cap_info |=
			IEEE80211_HT_EXT_CAP_PCO;
	temp_ht_cap = roam_ht_cap->transitionTime &
	    (IEEE80211_HT_EXT_CAP_PCO_TIME >>
	    IEEE80211_HT_EXT_CAP_PCO_TIME_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->extended_ht_cap_info |=
			temp_ht_cap << IEEE80211_HT_EXT_CAP_PCO_TIME_SHIFT;
	temp_ht_cap = roam_ht_cap->mcsFeedback &
	    (IEEE80211_HT_EXT_CAP_MCS_FB >> IEEE80211_HT_EXT_CAP_MCS_FB_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->extended_ht_cap_info |=
			temp_ht_cap << IEEE80211_HT_EXT_CAP_MCS_FB_SHIFT;

	/* tx_bf_cap_info capabilities */
	if (roam_ht_cap->txBF)
		hdd_ht_cap->tx_BF_cap_info |= TX_BF_CAP_INFO_TX_BF;
	if (roam_ht_cap->rxStaggeredSounding)
		hdd_ht_cap->tx_BF_cap_info |=
			TX_BF_CAP_INFO_RX_STAG_RED_SOUNDING;
	if (roam_ht_cap->txStaggeredSounding)
		hdd_ht_cap->tx_BF_cap_info |=
			TX_BF_CAP_INFO_TX_STAG_RED_SOUNDING;
	if (roam_ht_cap->rxZLF)
		hdd_ht_cap->tx_BF_cap_info |= TX_BF_CAP_INFO_RX_ZFL;
	if (roam_ht_cap->txZLF)
		hdd_ht_cap->tx_BF_cap_info |= TX_BF_CAP_INFO_TX_ZFL;
	if (roam_ht_cap->implicitTxBF)
		hdd_ht_cap->tx_BF_cap_info |= TX_BF_CAP_INFO_IMP_TX_BF;
	temp_ht_cap = roam_ht_cap->calibration &
	    (TX_BF_CAP_INFO_CALIBRATION >> TX_BF_CAP_INFO_CALIBRATION_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->tx_BF_cap_info |=
			temp_ht_cap << TX_BF_CAP_INFO_CALIBRATION_SHIFT;
	if (roam_ht_cap->explicitCSITxBF)
		hdd_ht_cap->tx_BF_cap_info |= TX_BF_CAP_INFO_EXP_CSIT_BF;
	if (roam_ht_cap->explicitUncompressedSteeringMatrix)
		hdd_ht_cap->tx_BF_cap_info |=
			TX_BF_CAP_INFO_EXP_UNCOMP_STEER_MAT;
	temp_ht_cap = roam_ht_cap->explicitBFCSIFeedback &
	    (TX_BF_CAP_INFO_EXP_BF_CSI_FB >>
	     TX_BF_CAP_INFO_EXP_BF_CSI_FB_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->tx_BF_cap_info |=
			temp_ht_cap << TX_BF_CAP_INFO_EXP_BF_CSI_FB_SHIFT;
	temp_ht_cap =
	    roam_ht_cap->explicitUncompressedSteeringMatrixFeedback &
	    (TX_BF_CAP_INFO_EXP_UNCMP_STEER_MAT >>
	     TX_BF_CAP_INFO_EXP_UNCMP_STEER_MAT_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->tx_BF_cap_info |=
			temp_ht_cap <<
			TX_BF_CAP_INFO_EXP_UNCMP_STEER_MAT_SHIFT;
	temp_ht_cap =
	    roam_ht_cap->explicitCompressedSteeringMatrixFeedback &
	    (TX_BF_CAP_INFO_EXP_CMP_STEER_MAT_FB >>
	     TX_BF_CAP_INFO_EXP_CMP_STEER_MAT_FB_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->tx_BF_cap_info |=
			temp_ht_cap <<
				TX_BF_CAP_INFO_EXP_CMP_STEER_MAT_FB_SHIFT;
	temp_ht_cap = roam_ht_cap->csiNumBFAntennae &
	    (TX_BF_CAP_INFO_CSI_NUM_BF_ANT >>
	     TX_BF_CAP_INFO_CSI_NUM_BF_ANT_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->tx_BF_cap_info |=
			temp_ht_cap << TX_BF_CAP_INFO_CSI_NUM_BF_ANT_SHIFT;
	temp_ht_cap = roam_ht_cap->uncompressedSteeringMatrixBFAntennae &
	    (TX_BF_CAP_INFO_UNCOMP_STEER_MAT_BF_ANT >>
	     TX_BF_CAP_INFO_UNCOMP_STEER_MAT_BF_ANT_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->tx_BF_cap_info |=
			temp_ht_cap <<
				TX_BF_CAP_INFO_UNCOMP_STEER_MAT_BF_ANT_SHIFT;
	temp_ht_cap = roam_ht_cap->compressedSteeringMatrixBFAntennae &
	    (TX_BF_CAP_INFO_COMP_STEER_MAT_BF_ANT >>
	     TX_BF_CAP_INFO_COMP_STEER_MAT_BF_ANT_SHIFT);
	if (temp_ht_cap)
		hdd_ht_cap->tx_BF_cap_info |=
			temp_ht_cap <<
				TX_BF_CAP_INFO_COMP_STEER_MAT_BF_ANT_SHIFT;

	/* antenna selection */
	if (roam_ht_cap->antennaSelection)
		hdd_ht_cap->antenna_selection_info |= ANTENNA_SEL_INFO;
	if (roam_ht_cap->explicitCSIFeedbackTx)
		hdd_ht_cap->antenna_selection_info |=
			ANTENNA_SEL_INFO_EXP_CSI_FB_TX;
	if (roam_ht_cap->antennaIndicesFeedbackTx)
		hdd_ht_cap->antenna_selection_info |=
			ANTENNA_SEL_INFO_ANT_ID_FB_TX;
	if (roam_ht_cap->explicitCSIFeedback)
		hdd_ht_cap->antenna_selection_info |=
			ANTENNA_SEL_INFO_EXP_CSI_FB;
	if (roam_ht_cap->antennaIndicesFeedback)
		hdd_ht_cap->antenna_selection_info |=
			ANTENNA_SEL_INFO_ANT_ID_FB;
	if (roam_ht_cap->rxAS)
		hdd_ht_cap->antenna_selection_info |=
			ANTENNA_SEL_INFO_RX_AS;
	if (roam_ht_cap->txSoundingPPDUs)
		hdd_ht_cap->antenna_selection_info |=
			ANTENNA_SEL_INFO_TX_SOUNDING_PPDU;

	/* mcs data rate */
	for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; ++i)
		hdd_ht_cap->mcs.rx_mask[i] =
			roam_ht_cap->supportedMCSSet[i];
	hdd_ht_cap->mcs.rx_highest =
			((short) (roam_ht_cap->supportedMCSSet[11]) << 8) |
			((short) (roam_ht_cap->supportedMCSSet[10]));
	hdd_ht_cap->mcs.tx_params =
			roam_ht_cap->supportedMCSSet[12];
}

#define VHT_CAP_MAX_MPDU_LENGTH_MASK 0x00000003
#define VHT_CAP_SUPP_CHAN_WIDTH_MASK_SHIFT 2
#define VHT_CAP_RXSTBC_MASK_SHIFT 8
#define VHT_CAP_BEAMFORMEE_STS_SHIFT 13
#define VHT_CAP_BEAMFORMEE_STS_MASK \
	(0x0000e000 >> VHT_CAP_BEAMFORMEE_STS_SHIFT)
#define VHT_CAP_SOUNDING_DIMENSIONS_SHIFT 16
#define VHT_CAP_SOUNDING_DIMENSIONS_MASK \
	(0x00070000 >> VHT_CAP_SOUNDING_DIMENSIONS_SHIFT)
#define VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK_SHIFT 23
#define VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK \
	(0x03800000 >> VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK_SHIFT)
#define VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB_SHIFT 26

/**
 * hdd_copy_ht_caps()- copy ht caps info from roam info to
 *  hdd station context.
 * @hdd_sta_ctx: pointer to hdd station context
 * @roam_info: pointer to roam info
 *
 * Return: None
 */
static void hdd_copy_vht_caps(hdd_station_ctx_t *hdd_sta_ctx,
				     tCsrRoamInfo *roam_info)
{
	tDot11fIEVHTCaps *roam_vht_cap = &roam_info->vht_caps;
	struct ieee80211_vht_cap *hdd_vht_cap =
		&hdd_sta_ctx->conn_info.vht_caps;
	uint32_t temp_vht_cap;

	adf_os_mem_zero(hdd_vht_cap, sizeof(struct ieee80211_vht_cap));

	temp_vht_cap = roam_vht_cap->maxMPDULen & VHT_CAP_MAX_MPDU_LENGTH_MASK;
	hdd_vht_cap->vht_cap_info |= temp_vht_cap;
	temp_vht_cap = roam_vht_cap->supportedChannelWidthSet &
		(IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_MASK >>
			VHT_CAP_SUPP_CHAN_WIDTH_MASK_SHIFT);
	if (temp_vht_cap) {
		if (roam_vht_cap->supportedChannelWidthSet &
		    (IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ >>
			VHT_CAP_SUPP_CHAN_WIDTH_MASK_SHIFT))
			hdd_vht_cap->vht_cap_info |=
				temp_vht_cap <<
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ;
		if (roam_vht_cap->supportedChannelWidthSet &
		    (IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ >>
			VHT_CAP_SUPP_CHAN_WIDTH_MASK_SHIFT))
			hdd_vht_cap->vht_cap_info |=
			temp_vht_cap <<
			IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ;
	}
	if (roam_vht_cap->ldpcCodingCap)
		hdd_vht_cap->vht_cap_info |= IEEE80211_VHT_CAP_RXLDPC;
	if (roam_vht_cap->shortGI80MHz)
		hdd_vht_cap->vht_cap_info |= IEEE80211_VHT_CAP_SHORT_GI_80;
	if (roam_vht_cap->shortGI160and80plus80MHz)
		hdd_vht_cap->vht_cap_info |= IEEE80211_VHT_CAP_SHORT_GI_160;
	if (roam_vht_cap->txSTBC)
		hdd_vht_cap->vht_cap_info |= IEEE80211_VHT_CAP_TXSTBC;
	temp_vht_cap = roam_vht_cap->rxSTBC & (IEEE80211_VHT_CAP_RXSTBC_MASK >>
		VHT_CAP_RXSTBC_MASK_SHIFT);
	if (temp_vht_cap)
		hdd_vht_cap->vht_cap_info |=
			temp_vht_cap << VHT_CAP_RXSTBC_MASK_SHIFT;
	if (roam_vht_cap->suBeamFormerCap)
		hdd_vht_cap->vht_cap_info |=
			IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE;
	if (roam_vht_cap->suBeamformeeCap)
		hdd_vht_cap->vht_cap_info |=
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
	temp_vht_cap = roam_vht_cap->csnofBeamformerAntSup &
			(VHT_CAP_BEAMFORMEE_STS_MASK);
	if (temp_vht_cap)
		hdd_vht_cap->vht_cap_info |=
			temp_vht_cap << VHT_CAP_BEAMFORMEE_STS_SHIFT;
	temp_vht_cap = roam_vht_cap->numSoundingDim &
			(VHT_CAP_SOUNDING_DIMENSIONS_MASK);
	if (temp_vht_cap)
		hdd_vht_cap->vht_cap_info |=
			temp_vht_cap << VHT_CAP_SOUNDING_DIMENSIONS_SHIFT;
	if (roam_vht_cap->muBeamformerCap)
		hdd_vht_cap->vht_cap_info |=
			IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE;
	if (roam_vht_cap->muBeamformeeCap)
		hdd_vht_cap->vht_cap_info |=
			IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;
	if (roam_vht_cap->vhtTXOPPS)
		hdd_vht_cap->vht_cap_info |=
			IEEE80211_VHT_CAP_VHT_TXOP_PS;
	if (roam_vht_cap->htcVHTCap)
		hdd_vht_cap->vht_cap_info |=
			IEEE80211_VHT_CAP_HTC_VHT;
	temp_vht_cap = roam_vht_cap->maxAMPDULenExp &
			(VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
	if (temp_vht_cap)
		hdd_vht_cap->vht_cap_info |=
			temp_vht_cap <<
			VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK_SHIFT;
	temp_vht_cap = roam_vht_cap->vhtLinkAdaptCap &
		(IEEE80211_VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB >>
		 VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB_SHIFT);
	if (temp_vht_cap)
		hdd_vht_cap->vht_cap_info |= temp_vht_cap <<
			VHT_CAP_VHT_LINK_ADAPTATION_VHT_MRQ_MFB_SHIFT;
	if (roam_vht_cap->rxAntPattern)
		hdd_vht_cap->vht_cap_info |=
			IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN;
	if (roam_vht_cap->txAntPattern)
		hdd_vht_cap->vht_cap_info |=
			IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN;
	hdd_vht_cap->supp_mcs.rx_mcs_map = roam_vht_cap->rxMCSMap;
	hdd_vht_cap->supp_mcs.rx_highest =
		((uint16_t)roam_vht_cap->rxHighSupDataRate);
	hdd_vht_cap->supp_mcs.tx_mcs_map = roam_vht_cap->txMCSMap;
	hdd_vht_cap->supp_mcs.tx_highest =
		((uint16_t)roam_vht_cap->txSupDataRate);
}

/* ht param */
#define HT_PARAM_CONTROLLED_ACCESS_ONLY 0x10
#define HT_PARAM_SERVICE_INT_GRAN 0xe0
#define HT_PARAM_SERVICE_INT_GRAN_SHIFT 5

/* operatinon mode */
#define HT_OP_MODE_TX_BURST_LIMIT 0x0008

/* stbc_param */
#define HT_STBC_PARAM_MCS 0x007f

/**
 * hdd_copy_ht_operation()- copy HT operation element from roam info to
 *  hdd station context.
 * @hdd_sta_ctx: pointer to hdd station context
 * @roam_info: pointer to roam info
 *
 * Return: None
 */
static void hdd_copy_ht_operation(hdd_station_ctx_t *hdd_sta_ctx,
					    tCsrRoamInfo *roam_info)
{
	tDot11fIEHTInfo *roam_ht_ops = &roam_info->ht_operation;
	struct ieee80211_ht_operation *hdd_ht_ops =
		&hdd_sta_ctx->conn_info.ht_operation;
	uint32_t i, temp_ht_ops;

	adf_os_mem_zero(hdd_ht_ops, sizeof(struct ieee80211_ht_operation));

	hdd_ht_ops->primary_chan = roam_ht_ops->primaryChannel;

	/* HT_PARAMS */
	temp_ht_ops = roam_ht_ops->secondaryChannelOffset &
		IEEE80211_HT_PARAM_CHA_SEC_OFFSET;
	if (temp_ht_ops)
		hdd_ht_ops->ht_param |= temp_ht_ops;
	else
		hdd_ht_ops->ht_param = IEEE80211_HT_PARAM_CHA_SEC_NONE;
	if (roam_ht_ops->recommendedTxWidthSet)
		hdd_ht_ops->ht_param |= IEEE80211_HT_PARAM_CHAN_WIDTH_ANY;
	if (roam_ht_ops->rifsMode)
		hdd_ht_ops->ht_param |= IEEE80211_HT_PARAM_RIFS_MODE;
	if (roam_ht_ops->controlledAccessOnly)
		hdd_ht_ops->ht_param |= HT_PARAM_CONTROLLED_ACCESS_ONLY;
	temp_ht_ops = roam_ht_ops->serviceIntervalGranularity &
		(HT_PARAM_SERVICE_INT_GRAN >> HT_PARAM_SERVICE_INT_GRAN_SHIFT);
	if (temp_ht_ops)
		hdd_ht_ops->ht_param |= temp_ht_ops <<
			HT_PARAM_SERVICE_INT_GRAN_SHIFT;

	/* operation mode */
	temp_ht_ops = roam_ht_ops->opMode &
			IEEE80211_HT_OP_MODE_PROTECTION;
	switch (temp_ht_ops) {
	case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
		hdd_ht_ops->operation_mode |=
			IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER;
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
		hdd_ht_ops->operation_mode |=
			IEEE80211_HT_OP_MODE_PROTECTION_20MHZ;
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
		hdd_ht_ops->operation_mode |=
			IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED;
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_NONE:
	default:
		hdd_ht_ops->operation_mode |=
			IEEE80211_HT_OP_MODE_PROTECTION_NONE;
	}
	if (roam_ht_ops->nonGFDevicesPresent)
		hdd_ht_ops->operation_mode |=
			IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT;
	if (roam_ht_ops->transmitBurstLimit)
		hdd_ht_ops->operation_mode |=
			HT_OP_MODE_TX_BURST_LIMIT;
	if (roam_ht_ops->obssNonHTStaPresent)
		hdd_ht_ops->operation_mode |=
			IEEE80211_HT_OP_MODE_NON_HT_STA_PRSNT;

	/* stbc_param */
	temp_ht_ops = roam_ht_ops->basicSTBCMCS &
			HT_STBC_PARAM_MCS;
	if (temp_ht_ops)
		hdd_ht_ops->stbc_param |= temp_ht_ops;
	if (roam_ht_ops->dualCTSProtection)
		hdd_ht_ops->stbc_param |=
			IEEE80211_HT_STBC_PARAM_DUAL_CTS_PROT;
	if (roam_ht_ops->secondaryBeacon)
		hdd_ht_ops->stbc_param |=
			IEEE80211_HT_STBC_PARAM_STBC_BEACON;
	if (roam_ht_ops->lsigTXOPProtectionFullSupport)
		hdd_ht_ops->stbc_param |=
			IEEE80211_HT_STBC_PARAM_LSIG_TXOP_FULLPROT;
	if (roam_ht_ops->pcoActive)
		hdd_ht_ops->stbc_param |=
			IEEE80211_HT_STBC_PARAM_PCO_ACTIVE;
	if (roam_ht_ops->pcoPhase)
		hdd_ht_ops->stbc_param |=
			IEEE80211_HT_STBC_PARAM_PCO_PHASE;

	/* basic MCs set */
	for (i = 0; i < 16; ++i)
		hdd_ht_ops->basic_set[i] =
			roam_ht_ops->basicMCSSet[i];
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
static void hdd_copy_vht_center_freq(struct ieee80211_vht_operation *ieee_ops,
				     tDot11fIEVHTOperation *roam_ops)
{
	ieee_ops->center_freq_seg0_idx = roam_ops->chanCenterFreqSeg1;
	ieee_ops->center_freq_seg1_idx = roam_ops->chanCenterFreqSeg2;
}
#else
static void hdd_copy_vht_center_freq(struct ieee80211_vht_operation *ieee_ops,
				     tDot11fIEVHTOperation *roam_ops)
{
	ieee_ops->center_freq_seg1_idx = roam_ops->chanCenterFreqSeg1;
	ieee_ops->center_freq_seg2_idx = roam_ops->chanCenterFreqSeg2;
}
#endif /* KERNEL_VERSION(4, 12, 0) */

/**
 * hdd_copy_vht_operation()- copy VHT operations element from roam info to
 *  hdd station context.
 * @hdd_sta_ctx: pointer to hdd station context
 * @roam_info: pointer to roam info
 *
 * Return: None
 */
static void hdd_copy_vht_operation(hdd_station_ctx_t *hdd_sta_ctx,
					      tCsrRoamInfo *roam_info)
{
	tDot11fIEVHTOperation *roam_vht_ops = &roam_info->vht_operation;
	struct ieee80211_vht_operation *hdd_vht_ops =
		&hdd_sta_ctx->conn_info.vht_operation;

	adf_os_mem_zero(hdd_vht_ops, sizeof(struct ieee80211_vht_operation));

	hdd_vht_ops->chan_width = roam_vht_ops->chanWidth;
	hdd_copy_vht_center_freq(hdd_vht_ops, roam_vht_ops);
	hdd_vht_ops->basic_mcs_set = roam_vht_ops->basicMCSSet;
}

/**
 * hdd_save_bss_info() - save connection info in hdd sta ctx
 * @adapter: Pointer to adapter
 * @roam_info: pointer to roam info
 *
 * Return: None
 */
static void hdd_save_bss_info(hdd_adapter_t *adapter,
						tCsrRoamInfo *roam_info)
{
	hdd_station_ctx_t *hdd_sta_ctx = WLAN_HDD_GET_STATION_CTX_PTR(adapter);

	hdd_sta_ctx->conn_info.freq = vos_chan_to_freq(
		hdd_sta_ctx->conn_info.operationChannel);
	if (roam_info->vht_caps.present) {
		hdd_sta_ctx->conn_info.conn_flag.vht_present = true;
		hdd_copy_vht_caps(hdd_sta_ctx, roam_info);
	} else {
		hdd_sta_ctx->conn_info.conn_flag.vht_present = false;
	}
	if (roam_info->ht_caps.present) {
		hdd_sta_ctx->conn_info.conn_flag.ht_present = true;
		hdd_copy_ht_caps(hdd_sta_ctx, roam_info);
	} else {
		hdd_sta_ctx->conn_info.conn_flag.ht_present = false;
	}
	if (roam_info->reassoc)
		hdd_sta_ctx->conn_info.roam_count++;
	if (roam_info->hs20vendor_ie.present) {
		hdd_sta_ctx->conn_info.conn_flag.hs20_present = true;
		adf_os_mem_copy(&hdd_sta_ctx->conn_info.hs20vendor_ie,
			     &roam_info->hs20vendor_ie,
			     sizeof(roam_info->hs20vendor_ie));
	} else {
		hdd_sta_ctx->conn_info.conn_flag.hs20_present = false;
	}
	if (roam_info->ht_operation.present) {
		hdd_sta_ctx->conn_info.conn_flag.ht_op_present = true;
		hdd_copy_ht_operation(hdd_sta_ctx, roam_info);
	} else {
		hdd_sta_ctx->conn_info.conn_flag.ht_op_present = false;
	}
	if (roam_info->vht_operation.present) {
		hdd_sta_ctx->conn_info.conn_flag.vht_op_present = true;
		hdd_copy_vht_operation(hdd_sta_ctx, roam_info);
	} else {
		hdd_sta_ctx->conn_info.conn_flag.vht_op_present = false;
	}
	/* Cache last connection info */
	vos_mem_copy(&hdd_sta_ctx->cache_conn_info, &hdd_sta_ctx->conn_info,
		     sizeof(connection_info_t));
}

static void
hdd_connSaveConnectInfo(hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo,
                        eCsrRoamBssType eBssType)
{
   hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
   eCsrEncryptionType encryptType = eCSR_ENCRYPT_TYPE_NONE;

   VOS_ASSERT( pRoamInfo );

   if ( pRoamInfo )
   {
      // Save the BSSID for the connection...
      if ( eCSR_BSS_TYPE_INFRASTRUCTURE == eBssType )
      {
          VOS_ASSERT( pRoamInfo->pBssDesc );
          vos_mem_copy(pHddStaCtx->conn_info.bssId, pRoamInfo->bssid,6 );

          // Save the Station ID for this station from the 'Roam Info'.
          //For IBSS mode, staId is assigned in NEW_PEER_IND
          //For reassoc, the staID doesn't change and it may be invalid in this structure
          //so no change here.
          if( !pRoamInfo->fReassocReq )
          {
              pHddStaCtx->conn_info.staId [0]= pRoamInfo->staId;
          }
      }
      else if ( eCSR_BSS_TYPE_IBSS == eBssType )
      {
         vos_mem_copy(pHddStaCtx->conn_info.bssId, pRoamInfo->bssid,sizeof(pRoamInfo->bssid) );
      }
      else
      {
         // can't happen.  We need a valid IBSS or Infra setting in the BSSDescription
         // or we can't function.
         VOS_ASSERT( 0 );
      }

      // notify WMM
      hdd_wmm_connect(pAdapter, pRoamInfo, eBssType);

      if( !pRoamInfo->u.pConnectedProfile )
      {
         VOS_ASSERT( pRoamInfo->u.pConnectedProfile );
      }
      else
      {
          // Get Multicast Encryption Type
          encryptType =  pRoamInfo->u.pConnectedProfile->mcEncryptionType;
          pHddStaCtx->conn_info.mcEncryptionType = encryptType;
          /* Get Unicast Encryption Type */
          encryptType =  pRoamInfo->u.pConnectedProfile->EncryptionType;
          pHddStaCtx->conn_info.ucEncryptionType = encryptType;

          pHddStaCtx->conn_info.authType =  pRoamInfo->u.pConnectedProfile->AuthType;

          pHddStaCtx->conn_info.operationChannel = pRoamInfo->u.pConnectedProfile->operationChannel;

          // Save the ssid for the connection
          vos_mem_copy( &pHddStaCtx->conn_info.SSID.SSID, &pRoamInfo->u.pConnectedProfile->SSID, sizeof( tSirMacSSid ) );

          // Save  dot11mode in which STA associated to AP
          pHddStaCtx->conn_info.dot11Mode = pRoamInfo->u.pConnectedProfile->dot11Mode;

          pHddStaCtx->conn_info.proxyARPService = pRoamInfo->u.pConnectedProfile->proxyARPService;
          pHddStaCtx->conn_info.nss        = pRoamInfo->chan_info.nss;
          pHddStaCtx->conn_info.rate_flags = pRoamInfo->chan_info.rate_flags;
      }
      hdd_save_bss_info(pAdapter, pRoamInfo);
   }

   // save the connected BssType
   hdd_connSaveConnectedBssType( pHddStaCtx, eBssType );

}

#if defined(WLAN_FEATURE_VOWIFI_11R)
/*
 * Send the 11R key information to the supplicant.
 * Only then can the supplicant generate the PMK-R1.
 * (BTW, the ESE supplicant also needs the Assoc Resp IEs
 * for the same purpose.)
 *
 * Mainly the Assoc Rsp IEs are passed here. For the IMDA
 * this contains the R1KHID, R0KHID and the MDID.
 * For FT, this consists of the Reassoc Rsp FTIEs.
 * This is the Assoc Response.
 */
static void hdd_SendFTAssocResponse(struct net_device *dev, hdd_adapter_t *pAdapter,
                tCsrRoamInfo *pCsrRoamInfo)
{
    union iwreq_data wrqu;
    char *buff;
    unsigned int len = 0;
    u8 *pFTAssocRsp = NULL;

    if (pCsrRoamInfo->nAssocRspLength < FT_ASSOC_RSP_IES_OFFSET)
    {
        hddLog(LOGE,
            "%s: pCsrRoamInfo->nAssocRspLength=%d",
            __func__, (int)pCsrRoamInfo->nAssocRspLength);
        return;
    }

    pFTAssocRsp = (u8 *)(pCsrRoamInfo->pbFrames + pCsrRoamInfo->nBeaconLength +
        pCsrRoamInfo->nAssocReqLength);
    if (pFTAssocRsp == NULL)
    {
        hddLog(LOGE, "%s: AssocReq or AssocRsp is NULL", __func__);
        return;
    }

    // pFTAssocRsp needs to point to the IEs
    pFTAssocRsp += FT_ASSOC_RSP_IES_OFFSET;

    // Send the Assoc Resp, the supplicant needs this for initial Auth.
    len = pCsrRoamInfo->nAssocRspLength - FT_ASSOC_RSP_IES_OFFSET;
    if (len > IW_GENERIC_IE_MAX)
    {
        hddLog(LOGE, "%s: Invalid assoc response IEs length %d",
               __func__, len);
        return;
    }
    wrqu.data.length = len;

    hddLog(LOG1, "%s: AssocRsp is now at %02x%02x", __func__,
        (unsigned int)pFTAssocRsp[0],
        (unsigned int)pFTAssocRsp[1]);

    // We need to send the IEs to the supplicant.
    buff = vos_mem_malloc(IW_GENERIC_IE_MAX);
    if (buff == NULL)
    {
        hddLog(LOGE, "%s: kmalloc unable to allocate memory", __func__);
        return;
    }

    memset(buff, 0, IW_GENERIC_IE_MAX);
    memcpy(buff, pFTAssocRsp, len);
    wireless_send_event(dev, IWEVASSOCRESPIE, &wrqu, buff);

    vos_mem_free(buff);
}
#endif /* WLAN_FEATURE_VOWIFI_11R */

#ifdef WLAN_FEATURE_VOWIFI_11R

/*---------------------------------------------------
 *
 * Send the FTIEs, RIC IEs during FT. This is eventually
 * used to send the FT events to the supplicant
 *
 * At the reception of Auth2 we send the RIC followed
 * by the auth response IEs to the supplicant.
 * Once both are received in the supplicant, an FT
 * event is generated to the supplicant.
 *
 *---------------------------------------------------
 */
void hdd_SendFTEvent(hdd_adapter_t *pAdapter)
{
    tANI_U16 auth_resp_len = 0;
    tANI_U32 ric_ies_length = 0;
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);

#if defined(KERNEL_SUPPORT_11R_CFG80211)
    struct cfg80211_ft_event_params ftEvent;
    v_U8_t ftIe[DOT11F_IE_FTINFO_MAX_LEN];
    v_U8_t ricIe[DOT11F_IE_RICDESCRIPTOR_MAX_LEN];
    struct net_device *dev = pAdapter->dev;
#else
    char *buff;
    union iwreq_data wrqu;
    tANI_U16 str_len;
#endif

#if defined(KERNEL_SUPPORT_11R_CFG80211)
    vos_mem_zero(ftIe, DOT11F_IE_FTINFO_MAX_LEN);
    vos_mem_zero(ricIe, DOT11F_IE_RICDESCRIPTOR_MAX_LEN);

    sme_GetRICIEs( pHddCtx->hHal, pAdapter->sessionId, (u8 *)ricIe,
                  DOT11F_IE_RICDESCRIPTOR_MAX_LEN, &ric_ies_length );
    if (ric_ies_length == 0)
    {
        hddLog(LOGW,
              "%s: RIC IEs is of length 0 not sending RIC Information for now",
              __func__);
    }

    ftEvent.ric_ies = ricIe;
    ftEvent.ric_ies_len = ric_ies_length;
    hddLog(LOG1, "%s: RIC IEs is of length %d", __func__, (int)ric_ies_length);

    sme_GetFTPreAuthResponse(pHddCtx->hHal, pAdapter->sessionId, (u8 *)ftIe,
                DOT11F_IE_FTINFO_MAX_LEN, &auth_resp_len);

    if (auth_resp_len == 0)
    {
        hddLog(LOGE, "%s: AuthRsp FTIES is of length 0", __func__);
        return;
    }

    sme_SetFTPreAuthState(pHddCtx->hHal, pAdapter->sessionId, VOS_TRUE);

    ftEvent.target_ap = ftIe;

    ftEvent.ies = (u8 *)(ftIe + SIR_MAC_ADDR_LENGTH);
    ftEvent.ies_len = auth_resp_len - SIR_MAC_ADDR_LENGTH;

    hddLog(LOG1, "%s ftEvent.ies_len %zu", __FUNCTION__, ftEvent.ies_len);
    hddLog(LOG1, "%s ftEvent.ric_ies_len %zu",
           __FUNCTION__, ftEvent.ric_ies_len );
    hddLog(LOG1, "%s ftEvent.target_ap %2x-%2x-%2x-%2x-%2x-%2x ",
            __FUNCTION__, ftEvent.target_ap[0], ftEvent.target_ap[1],
            ftEvent.target_ap[2], ftEvent.target_ap[3], ftEvent.target_ap[4],
            ftEvent.target_ap[5]);

    (void)cfg80211_ft_event(dev, &ftEvent);

#else
    // We need to send the IEs to the supplicant
    buff = vos_mem_malloc(IW_CUSTOM_MAX);
    if (buff == NULL)
    {
        hddLog(LOGE, "%s: kmalloc unable to allocate memory", __func__);
        return;
    }
    vos_mem_zero(buff, IW_CUSTOM_MAX);

    // Sme needs to send the RIC IEs first
    str_len = strlcpy(buff, "RIC=", IW_CUSTOM_MAX);
    sme_GetRICIEs( pHddCtx->hHal, pAdapter->sessionId, (u8 *)&(buff[str_len]),
            (IW_CUSTOM_MAX - str_len), &ric_ies_length );
    if (ric_ies_length == 0)
    {
        hddLog(LOGW,
               "%s: RIC IEs is of length 0 not sending RIC Information for now",
               __func__);
    }
    else
    {
        wrqu.data.length = str_len + ric_ies_length;
        wireless_send_event(pAdapter->dev, IWEVCUSTOM, &wrqu, buff);
    }

    // Sme needs to provide the Auth Resp
    vos_mem_zero(buff, IW_CUSTOM_MAX);
    str_len = strlcpy(buff, "AUTH=", IW_CUSTOM_MAX);
    sme_GetFTPreAuthResponse(pHddCtx->hHal, pAdapter->sessionId,
                            (u8 *)&buff[str_len],
                            (IW_CUSTOM_MAX - str_len),
                            &auth_resp_len);

    if (auth_resp_len == 0)
    {
        hddLog(LOGE, "%s: AuthRsp FTIES is of length 0", __func__);
        vos_mem_free(buff);
        return;
    }

    wrqu.data.length = str_len + auth_resp_len;
    wireless_send_event(pAdapter->dev, IWEVCUSTOM, &wrqu, buff);

    vos_mem_free(buff);
#endif
}

#endif /* WLAN_FEATURE_VOWIFI_11R */

#ifdef FEATURE_WLAN_ESE

/*
 * Send the ESE required "new AP Channel info" to the supplicant.
 * (This keeps the supplicant "up to date" on the current channel.)
 *
 * The current (new AP) channel information is passed in.
 */
static void hdd_SendNewAPChannelInfo(struct net_device *dev, hdd_adapter_t *pAdapter,
                tCsrRoamInfo *pCsrRoamInfo)
{
    union iwreq_data wrqu;
    tSirBssDescription *descriptor = pCsrRoamInfo->pBssDesc;


    if (descriptor == NULL)
    {
        hddLog(LOGE,
            "%s: pCsrRoamInfo->pBssDesc=%pK",
            __func__, descriptor);
        return;
    }

    // Send the Channel event, the supplicant needs this to generate the Adjacent AP report.
    hddLog(LOGW, "%s: Sending up an SIOCGIWFREQ, channelId=%d", __func__, descriptor->channelId);
    memset(&wrqu, '\0', sizeof(wrqu));
    wrqu.freq.m = descriptor->channelId;
    wrqu.freq.e = 0;
    wrqu.freq.i = 0;
    wireless_send_event(pAdapter->dev, SIOCGIWFREQ, &wrqu, NULL);
}

#endif /* FEATURE_WLAN_ESE */

static void
hdd_SendUpdateBeaconIEsEvent(hdd_adapter_t *pAdapter,
                             tCsrRoamInfo *pCsrRoamInfo)
{
    union iwreq_data wrqu;
    u8  *pBeaconIes;
    u8 currentLen = 0;
    char *buff;
    int totalIeLen = 0, currentOffset = 0, strLen;

    memset(&wrqu, '\0', sizeof(wrqu));

    if (0 == pCsrRoamInfo->nBeaconLength)
    {
        hddLog(LOGW, "%s: pCsrRoamInfo->nBeaconFrameLength = 0", __func__);
        return;
    }
    pBeaconIes = (u8 *)(pCsrRoamInfo->pbFrames + BEACON_FRAME_IES_OFFSET);
    if (pBeaconIes == NULL)
    {
        hddLog(LOGW, "%s: Beacon IEs is NULL", __func__);
        return;
    }

    // pBeaconIes needs to point to the IEs
    hddLog(LOG1, "%s: Beacon IEs is now at %02x%02x", __func__,
        (unsigned int)pBeaconIes[0],
        (unsigned int)pBeaconIes[1]);
    hddLog(LOG1, "%s: Beacon IEs length = %d", __func__, pCsrRoamInfo->nBeaconLength - BEACON_FRAME_IES_OFFSET);

   // We need to send the IEs to the supplicant.
    buff = vos_mem_malloc(IW_CUSTOM_MAX);
    if (buff == NULL)
    {
        hddLog(LOGE, "%s: kmalloc unable to allocate memory", __func__);
        return;
    }
    vos_mem_zero(buff, IW_CUSTOM_MAX);

    strLen = strlcpy(buff,"BEACONIEs=", IW_CUSTOM_MAX);
    currentLen = strLen + 1;

    totalIeLen = pCsrRoamInfo->nBeaconLength - BEACON_FRAME_IES_OFFSET;
    do
    {
        /* If the beacon size exceeds max CUSTOM event size, break it into chunks of CUSTOM event
         * max size and send it to supplicant. Changes are done in supplicant to handle this */
        vos_mem_zero(&buff[strLen + 1], IW_CUSTOM_MAX - (strLen + 1));
        currentLen = VOS_MIN(totalIeLen, IW_CUSTOM_MAX - (strLen + 1) - 1);
        vos_mem_copy(&buff[strLen + 1], pBeaconIes+currentOffset, currentLen);
        currentOffset += currentLen;
        totalIeLen -= currentLen;
        wrqu.data.length = strLen + 1 + currentLen;
        if (totalIeLen)
          buff[strLen] = 1;   // This tells supplicant more chunks are pending
        else
          buff[strLen] = 0;   // For last chunk of beacon IE to supplicant

        hddLog(LOG1, "%s: Beacon IEs length to supplicant = %d", __func__, currentLen);
        wireless_send_event(pAdapter->dev, IWEVCUSTOM, &wrqu, buff);
    } while (totalIeLen > 0);

    vos_mem_free(buff);
}

static void hdd_SendAssociationEvent(struct net_device *dev,tCsrRoamInfo *pCsrRoamInfo)
{
    hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
    hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
    union iwreq_data wrqu;
    int we_event;
    char *msg;
    int type = -1;
    v_MACADDR_t peerMacAddr;

    hdd_adapter_t *mon_adapter = NULL;
#if defined (WLAN_FEATURE_VOWIFI_11R)
    // Added to find the auth type on the fly at run time
    // rather than with cfg to see if FT is enabled
    hdd_wext_state_t  *pWextState =  WLAN_HDD_GET_WEXT_STATE_PTR(pAdapter);
    tCsrRoamProfile* pRoamProfile = &(pWextState->roamProfile);
#endif

    memset(&wrqu, '\0', sizeof(wrqu));
    wrqu.ap_addr.sa_family = ARPHRD_ETHER;
    we_event = SIOCGIWAP;
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
    if (NULL != pCsrRoamInfo)
        if (pCsrRoamInfo->roamSynchInProgress)
            /* change logging before release */
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_DEBUG,
                      "LFR3:hdd_SendAssociationEvent");
#endif
    if(eConnectionState_Associated == pHddStaCtx->conn_info.connState)/* Associated */
    {
        tSirSmeChanInfo chan_info;
        if (!pCsrRoamInfo)
        {
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: STA in associated state but pCsrRoamInfo is null",
                      __func__);
            return;
        }

        wlan_hdd_incr_active_session(pHddCtx, pAdapter->device_mode);
        memcpy(wrqu.ap_addr.sa_data, pCsrRoamInfo->pBssDesc->bssId, sizeof(pCsrRoamInfo->pBssDesc->bssId));
        type = WLAN_STA_ASSOC_DONE_IND;

#ifdef WLAN_FEATURE_P2P_DEBUG
        if(pAdapter->device_mode == WLAN_HDD_P2P_CLIENT)
        {
             if(globalP2PConnectionStatus == P2P_CLIENT_CONNECTING_STATE_1)
             {
                 globalP2PConnectionStatus = P2P_CLIENT_CONNECTED_STATE_1;
                 hddLog(VOS_TRACE_LEVEL_ERROR,"[P2P State] Changing state from "
                                "Connecting state to Connected State for 8-way "
                                "Handshake");
             }
             else if(globalP2PConnectionStatus == P2P_CLIENT_CONNECTING_STATE_2)
             {
                 globalP2PConnectionStatus = P2P_CLIENT_COMPLETED_STATE;
                 hddLog(VOS_TRACE_LEVEL_ERROR,"[P2P State] Changing state from "
                           "Connecting state to P2P Client Connection Completed");
             }
        }
#endif
        hddLog(VOS_TRACE_LEVEL_ERROR, MAC_ADDRESS_STR " connected to "
                MAC_ADDRESS_STR,
                MAC_ADDR_ARRAY(pAdapter->macAddressCurrent.bytes),
                MAC_ADDR_ARRAY(wrqu.ap_addr.sa_data));
        hdd_SendUpdateBeaconIEsEvent(pAdapter, pCsrRoamInfo);


        /* Send IWEVASSOCRESPIE Event if WLAN_FEATURE_CIQ_METRICS is Enabled Or
         * Send IWEVASSOCRESPIE Event if WLAN_FEATURE_VOWIFI_11R is Enabled
         * and fFTEnable is TRUE */
#ifdef WLAN_FEATURE_VOWIFI_11R
        // Send FT Keys to the supplicant when FT is enabled
        if ((pRoamProfile->AuthType.authType[0] == eCSR_AUTH_TYPE_FT_RSN_PSK) ||
            (pRoamProfile->AuthType.authType[0] == eCSR_AUTH_TYPE_FT_RSN)
#ifdef FEATURE_WLAN_ESE
            || (pRoamProfile->AuthType.authType[0] == eCSR_AUTH_TYPE_CCKM_RSN) ||
            (pRoamProfile->AuthType.authType[0] == eCSR_AUTH_TYPE_CCKM_WPA)
#endif
        )
        {
            hdd_SendFTAssocResponse(dev, pAdapter, pCsrRoamInfo);
        }
#endif

        vos_mem_copy(peerMacAddr.bytes, pHddStaCtx->conn_info.bssId,
                     VOS_MAC_ADDR_SIZE);
        chan_info.chan_id = pCsrRoamInfo->chan_info.chan_id;
        chan_info.mhz = pCsrRoamInfo->chan_info.mhz;
        chan_info.info = pCsrRoamInfo->chan_info.info;
        chan_info.band_center_freq1 =
                   pCsrRoamInfo->chan_info.band_center_freq1;
        chan_info.band_center_freq2 =
                   pCsrRoamInfo->chan_info.band_center_freq2;
        chan_info.reg_info_1 = pCsrRoamInfo->chan_info.reg_info_1;
        chan_info.reg_info_2 = pCsrRoamInfo->chan_info.reg_info_2;

        /* send peer status indication to oem app */
        hdd_SendPeerStatusIndToOemApp(&peerMacAddr, ePeerConnected,
                                      pCsrRoamInfo->timingMeasCap,
                                      pAdapter->sessionId,
                                      &chan_info,
                                      pAdapter->device_mode);

#ifdef FEATURE_WLAN_TDLS
        if (pAdapter->device_mode == WLAN_HDD_INFRA_STATION) {
            hddLog(LOG1,
                   FL("tdls_prohibited: %d, tdls_chan_swit_prohibited: %d"),
                   pCsrRoamInfo->tdls_prohibited,
                   pCsrRoamInfo->tdls_chan_swit_prohibited);

            wlan_hdd_update_tdls_info(pAdapter, pCsrRoamInfo->tdls_prohibited,
                                      pCsrRoamInfo->tdls_chan_swit_prohibited);
        }
#endif

#ifdef FEATURE_BUS_BANDWIDTH
        /* start timer in sta/p2p_cli */
        adf_os_spin_lock_bh(&pHddCtx->bus_bw_lock);
        pAdapter->prev_tx_packets = pAdapter->stats.tx_packets;
        pAdapter->prev_rx_packets = pAdapter->stats.rx_packets;
        tlshim_get_intra_bss_fwd_pkts_count(pAdapter->sessionId,
             &pAdapter->prev_fwd_tx_packets, &pAdapter->prev_fwd_rx_packets);
        pAdapter->prev_tx_bytes = pAdapter->stats.tx_bytes;
        adf_os_spin_unlock_bh(&pHddCtx->bus_bw_lock);
        hdd_start_bus_bw_compute_timer(pAdapter);
#endif
        if (pHddCtx->cfg_ini->mon_on_sta_enable &&
            (pAdapter->device_mode == WLAN_HDD_INFRA_STATION)) {
            /* monitor mode interface should be ready */
            mon_adapter = hdd_get_adapter(pHddCtx, WLAN_HDD_MONITOR);

            if (mon_adapter && (WLAN_HDD_ADAPTER_MAGIC == mon_adapter->magic) &&
                tlshim_get_rxmon_cbk()) {
               wlan_hdd_monitor_mode_enable(pHddCtx, true);
           }
        }
    }
    else if (eConnectionState_IbssConnected == pHddStaCtx->conn_info.connState) // IBss Associated
    {
        wlan_hdd_incr_active_session(pHddCtx, pAdapter->device_mode);
        memcpy(wrqu.ap_addr.sa_data, pHddStaCtx->conn_info.bssId, ETH_ALEN);
        type = WLAN_STA_ASSOC_DONE_IND;

        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                 "wlan: new IBSS connection to " MAC_ADDRESS_STR,
                 MAC_ADDR_ARRAY(pHddStaCtx->conn_info.bssId));
    }
    else /* Not Associated */
    {
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                 "wlan: disconnected");
        type = WLAN_STA_DISASSOC_DONE_IND;
        memset(wrqu.ap_addr.sa_data,'\0',ETH_ALEN);
        wlan_hdd_decr_active_session(pHddCtx, pAdapter->device_mode);
#if defined(FEATURE_WLAN_LFR) && defined(WLAN_FEATURE_ROAM_SCAN_OFFLOAD)
        wlan_hdd_enable_roaming(pAdapter);
#endif

#ifdef FEATURE_WLAN_AUTO_SHUTDOWN
        wlan_hdd_auto_shutdown_enable(pHddCtx, VOS_TRUE);
#endif

        if ((WLAN_HDD_P2P_CLIENT == pAdapter->device_mode) ||
           (WLAN_HDD_INFRA_STATION == pAdapter->device_mode)) {
             vos_mem_copy(peerMacAddr.bytes, pHddStaCtx->conn_info.bssId,
                         sizeof(pHddStaCtx->conn_info.bssId));

             /* send peer status indication to oem app */
             hdd_SendPeerStatusIndToOemApp(&peerMacAddr, ePeerDisconnected,
                                          0, pAdapter->sessionId,
                                          NULL,
                                          pAdapter->device_mode);
        }

#ifdef WLAN_FEATURE_LPSS
        pAdapter->rssi_send = VOS_FALSE;
        if (pHddCtx->isUnloadInProgress != TRUE)
                wlan_hdd_send_status_pkg(pAdapter, pHddStaCtx, 1, 0);
#endif

#ifdef FEATURE_BUS_BANDWIDTH
        /* stop timer in sta/p2p_cli */
        adf_os_spin_lock_bh(&pHddCtx->bus_bw_lock);
        pAdapter->prev_tx_packets = 0;
        pAdapter->prev_rx_packets = 0;
        pAdapter->prev_fwd_tx_packets = 0;
        pAdapter->prev_fwd_rx_packets = 0;
        pAdapter->prev_tx_bytes = 0;
        adf_os_spin_unlock_bh(&pHddCtx->bus_bw_lock);
        hdd_stop_bus_bw_compute_timer(pAdapter);
#endif
        if (pHddCtx->cfg_ini->mon_on_sta_enable &&
            (pAdapter->device_mode == WLAN_HDD_INFRA_STATION) &&
            (true == pHddCtx->is_mon_enable)) {
            wlan_hdd_monitor_mode_enable(pHddCtx, false);
        }
    }
    hdd_dump_concurrency_info(pHddCtx);

    msg = NULL;
    /*During the WLAN uninitialization,supplicant is stopped before the
      driver so not sending the status of the connection to supplicant*/
    if ((pHddCtx->isLoadInProgress != TRUE) &&
        (pHddCtx->isUnloadInProgress != TRUE))
    {
        wireless_send_event(dev, we_event, &wrqu, msg);
#ifdef FEATURE_WLAN_ESE
        if(eConnectionState_Associated == pHddStaCtx->conn_info.connState)/* Associated */
        {
            if ( (pRoamProfile->AuthType.authType[0] == eCSR_AUTH_TYPE_CCKM_RSN) ||
                (pRoamProfile->AuthType.authType[0] == eCSR_AUTH_TYPE_CCKM_WPA) )
            hdd_SendNewAPChannelInfo(dev, pAdapter, pCsrRoamInfo);
        }
#endif
    }
}

static void hdd_connRemoveConnectInfo(hdd_station_ctx_t *pHddStaCtx)
{
   // Remove staId, bssId and peerMacAddress
   pHddStaCtx->conn_info.staId [ 0 ] = 0;
   vos_mem_zero( &pHddStaCtx->conn_info.bssId, sizeof( v_MACADDR_t ) );
   vos_mem_zero( &pHddStaCtx->conn_info.peerMacAddress[ 0 ], sizeof( v_MACADDR_t ) );

   // Clear all security settings
   pHddStaCtx->conn_info.authType         = eCSR_AUTH_TYPE_OPEN_SYSTEM;
   pHddStaCtx->conn_info.mcEncryptionType = eCSR_ENCRYPT_TYPE_NONE;
   pHddStaCtx->conn_info.ucEncryptionType = eCSR_ENCRYPT_TYPE_NONE;

   vos_mem_zero( &pHddStaCtx->conn_info.Keys, sizeof( tCsrKeys ) );
   vos_mem_zero( &pHddStaCtx->ibss_enc_key, sizeof(tCsrRoamSetKey) );

   // Set not-connected state
   pHddStaCtx->conn_info.connDot11DesiredBssType = eCSR_BSS_TYPE_ANY;
   pHddStaCtx->conn_info.proxyARPService = 0;

   vos_mem_zero( &pHddStaCtx->conn_info.SSID, sizeof( tCsrSSIDInfo ) );
}

/**
 * hdd_roamDeregisterSTA() - Deregister STA from data path
 * @pAdapter - HDD context
 * @staId - Station ID
 *
 * Return: 0 or VOS_STATUS error code
 */
VOS_STATUS hdd_roamDeregisterSTA(hdd_adapter_t *pAdapter, tANI_U8 staId)
{
    VOS_STATUS vosStatus;
    hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);

    if (WLAN_HDD_IBSS != pAdapter->device_mode)
    {
       hdd_disconnect_tx_rx(pAdapter);
    }
    else
    {
       // Need to cleanup all queues only if the last peer leaves
       if (eConnectionState_IbssDisconnected == pHddStaCtx->conn_info.connState)
       {
       /* Do not set the carrier off when the last peer leaves.
        * We will set the carrier off while stopping the IBSS.
        */
          hdd_disconnect_tx_rx(pAdapter);
       }
       else
       {
          // There is atleast one more peer, do not cleanup all queues
          hdd_flush_ibss_tx_queues(pAdapter, staId);
       }
    }

    vosStatus = WLANTL_ClearSTAClient( (WLAN_HDD_GET_CTX(pAdapter))->pvosContext, staId );
    if ( !VOS_IS_STATUS_SUCCESS( vosStatus ) )
    {
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                   "%s: WLANTL_ClearSTAClient() failed to for staID %d.  "
                   "Status= %d [0x%08X]",
                   __func__, staId, vosStatus, vosStatus );
    }
    return( vosStatus );
}

/**
 * hdd_print_bss_info() - print bss info
 * @hdd_sta_ctx: pointer to hdd station context
 *
 * Return: None
 */
static void hdd_print_bss_info(hdd_station_ctx_t *hdd_sta_ctx)
{
	uint32_t *cap_info;

	hddLog(VOS_TRACE_LEVEL_INFO, "WIFI DATA LOGGER");
	hddLog(VOS_TRACE_LEVEL_INFO, "channel: %d",
	       hdd_sta_ctx->conn_info.freq);
	hddLog(VOS_TRACE_LEVEL_INFO, "dot11mode: %d",
	       hdd_sta_ctx->conn_info.dot11Mode);
	hddLog(VOS_TRACE_LEVEL_INFO, "AKM: %d",
	       hdd_sta_ctx->cache_conn_info.authType);
	hddLog(VOS_TRACE_LEVEL_INFO, "ssid: %.*s",
	       hdd_sta_ctx->cache_conn_info.SSID.SSID.length,
	       hdd_sta_ctx->cache_conn_info.SSID.SSID.ssId);
	hddLog(VOS_TRACE_LEVEL_INFO, "roam count: %d",
	       hdd_sta_ctx->conn_info.roam_count);
	hddLog(VOS_TRACE_LEVEL_INFO, "ant_info: %d",
	       hdd_sta_ctx->conn_info.txrate.nss);
	hddLog(VOS_TRACE_LEVEL_INFO, "datarate legacy %d",
	       hdd_sta_ctx->conn_info.txrate.legacy);
	hddLog(VOS_TRACE_LEVEL_INFO, "datarate mcs: %d",
	       hdd_sta_ctx->conn_info.txrate.mcs);
	if (hdd_sta_ctx->conn_info.conn_flag.ht_present) {
		cap_info = (uint32_t *)&hdd_sta_ctx->conn_info.ht_caps;
		hddLog(VOS_TRACE_LEVEL_INFO, "ht caps: %x", *cap_info);
	}
	if (hdd_sta_ctx->conn_info.conn_flag.vht_present) {
		cap_info = (uint32_t *)&hdd_sta_ctx->conn_info.vht_caps;
		hddLog(VOS_TRACE_LEVEL_INFO, "vht caps: %x", *cap_info);
	}
	if (hdd_sta_ctx->conn_info.conn_flag.hs20_present)
		hddLog(VOS_TRACE_LEVEL_INFO, "hs20 info: %x",
		       hdd_sta_ctx->conn_info.hs20vendor_ie.release_num);
	hddLog(VOS_TRACE_LEVEL_INFO, "signal: %d",
	       hdd_sta_ctx->conn_info.signal);
	hddLog(VOS_TRACE_LEVEL_INFO, "noise: %d",
	       hdd_sta_ctx->conn_info.noise);
	hddLog(VOS_TRACE_LEVEL_INFO, "assoc_reject.status: %d",
	       hdd_sta_ctx->conn_info.assoc_status_code);
}

static eHalStatus hdd_DisConnectHandler( hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo,
                                            tANI_U32 roamId, eRoamCmdStatus roamStatus,
                                            eCsrRoamResult roamResult )
{
    eHalStatus status = eHAL_STATUS_SUCCESS;
    VOS_STATUS vstatus;
    struct net_device *dev = pAdapter->dev;
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
    hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
    v_U8_t sta_id;
    v_BOOL_t sendDisconInd = TRUE;

    // Sanity check
    if(dev == NULL)
    {
        hddLog(VOS_TRACE_LEVEL_ERROR,
               "%s: net_dev is released return", __func__);
        return eHAL_STATUS_FAILURE;
    }

    // notify apps that we can't pass traffic anymore
    hddLog(LOG1, FL("Disabling queues"));
    wlan_hdd_netif_queue_control(pAdapter, WLAN_NETIF_TX_DISABLE_N_CARRIER,
                      WLAN_CONTROL_PATH);

#ifdef IPA_OFFLOAD
    if (hdd_ipa_is_enabled(pHddCtx))
        hdd_ipa_wlan_evt(pAdapter, pHddStaCtx->conn_info.staId[0],
                WLAN_STA_DISCONNECT, pHddStaCtx->conn_info.bssId);
#endif
#ifdef FEATURE_WLAN_AUTO_SHUTDOWN
        wlan_hdd_auto_shutdown_enable(pHddCtx, VOS_TRUE);
#endif

#ifdef QCA_PKT_PROTO_TRACE
     /* STA disconnected, update into trace buffer */
     if (pHddCtx->cfg_ini->gEnableDebugLog)
     {
        vos_pkt_trace_buf_update("ST:DISASC");
     }
#endif /* QCA_PKT_PROTO_TRACE */

    DPTRACE(adf_dp_trace_mgmt_pkt(ADF_DP_TRACE_MGMT_PACKET_RECORD,
                   pAdapter->sessionId,
                   ADF_PROTO_TYPE_MGMT, ADF_PROTO_MGMT_DISASSOC));

    /* HDD has initiated disconnect, do not send disconnect indication
     * to kernel. Sending disconnected event to kernel for userspace
     * initiated disconnect will be handled by diconnect handler call
     * to cfg80211_disconnected
     */
    if ((eConnectionState_Disconnecting == pHddStaCtx->conn_info.connState) ||
        (eConnectionState_NotConnected == pHddStaCtx->conn_info.connState))
    {
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                   FL(" HDD has initiated a disconnect, no need to send"
                   " disconnect indication to kernel"));
       sendDisconInd = FALSE;
    }

    if(pHddStaCtx->conn_info.connState != eConnectionState_Disconnecting)
    {
        INIT_COMPLETION(pAdapter->disconnect_comp_var);
        hdd_connSetConnectionState(pAdapter,
                   eConnectionState_Disconnecting);
    }

    /* If only STA mode is on */
    if((pHddCtx->concurrency_mode <= 1) &&
       (pHddCtx->no_of_open_sessions[WLAN_HDD_INFRA_STATION] <=1))
    {
        pHddCtx->isAmpAllowed = VOS_TRUE;
    }
    hdd_clearRoamProfileIe( pAdapter );
    hdd_wmm_init( pAdapter );
    hddLog(VOS_TRACE_LEVEL_INFO,
           FL("Invoking packetdump deregistration API"));
    wlan_deregister_txrx_packetdump();
    // indicate 'disconnect' status to wpa_supplicant...
    hdd_SendAssociationEvent(dev,pRoamInfo);
    /* indicate disconnected event to nl80211 */
    if(roamStatus != eCSR_ROAM_IBSS_LEAVE)
    {
        /* Only send indication to kernel if not initiated by kernel */
        if (sendDisconInd) {
            /* To avoid wpa_supplicant sending "HANGED" CMD to ICS UI */
            if (eCSR_ROAM_LOSTLINK == roamStatus)
            {
               if (pRoamInfo->reasonCode ==
                               eSIR_MAC_PEER_STA_REQ_LEAVING_BSS_REASON)
                       pr_info(
                       "wlan: disconnected due to poor signal, rssi is %d dB\n",
                       pRoamInfo->rxRssi);
               wlan_hdd_cfg80211_indicate_disconnect(dev, false,
                                                     pRoamInfo->reasonCode);
            }
            else
               wlan_hdd_cfg80211_indicate_disconnect(dev, false,
                                                     WLAN_REASON_UNSPECIFIED);

            hddLog(VOS_TRACE_LEVEL_INFO_HIGH,
                   FL("sent disconnected event to nl80211, reason code %d"),
                      (eCSR_ROAM_LOSTLINK == roamStatus) ?
                      pRoamInfo->reasonCode : WLAN_REASON_UNSPECIFIED);
        }

        if ((pHddCtx->isLoadInProgress != TRUE) &&
            (pHddCtx->isUnloadInProgress != TRUE))
        {
#ifdef WLAN_FEATURE_P2P_DEBUG
            if(pAdapter->device_mode == WLAN_HDD_P2P_CLIENT)
            {
                if(globalP2PConnectionStatus == P2P_CLIENT_CONNECTED_STATE_1)
                {
                    globalP2PConnectionStatus = P2P_CLIENT_DISCONNECTED_STATE;
                    hddLog(VOS_TRACE_LEVEL_ERROR,"[P2P State] 8 way Handshake completed "
                          "and moved to disconnected state");
                }
                else if(globalP2PConnectionStatus == P2P_CLIENT_COMPLETED_STATE)
                {
                    globalP2PConnectionStatus = P2P_NOT_ACTIVE;
                    hddLog(VOS_TRACE_LEVEL_ERROR,"[P2P State] P2P Client is removed "
                          "and moved to inactive state");
                }
            }
#endif

#ifdef WLAN_FEATURE_SAP_TO_FOLLOW_STA_CHAN
            if((pAdapter->device_mode == WLAN_HDD_INFRA_STATION) &&
                    vos_is_ch_switch_with_csa_enabled())
            {
                struct wlan_sap_csa_info csa_info;
                hddLog(VOS_TRACE_LEVEL_INFO_HIGH,
                    "%s: Indicate disconnected event to HostApd",
                    __func__);

                csa_info.sta_channel = 0;
                /*Indicate to HostApd about Station interface state change*/
                hdd_sta_state_sap_notify(pHddCtx, STA_NOTIFY_DISCONNECTED, csa_info);
            }
#endif//#ifdef WLAN_FEATURE_SAP_TO_FOLLOW_STA_CHAN

            //If the Device Mode is Station
            // and the P2P Client is Connected
            //Enable BMPS

            /*
             * In case of JB, as Change-Iface may or may not be called for p2p0
             * Enable BMPS/IMPS in case P2P_CLIENT disconnected
             * If power save offload is enabled, Fw will take care
             * of power save in case of concurrency.
             */
            if((VOS_STATUS_SUCCESS == hdd_issta_p2p_clientconnected(pHddCtx))
               && !pHddCtx->cfg_ini->enablePowersaveOffload)
            {
               //Enable BMPS only of other Session is P2P Client
               hdd_context_t *pHddCtx = NULL;
               v_CONTEXT_t pVosContext = vos_get_global_context( VOS_MODULE_ID_HDD, NULL );

               if (NULL != pVosContext)
               {
                   pHddCtx = vos_get_context( VOS_MODULE_ID_HDD, pVosContext);

                   if(NULL != pHddCtx)
                   {
                       //Only P2P Client is there Enable Bmps back
                       if((0 == pHddCtx->no_of_open_sessions[VOS_STA_SAP_MODE]) &&
                          (0 == pHddCtx->no_of_open_sessions[VOS_P2P_GO_MODE]))
                       {
                           if (pHddCtx->hdd_wlan_suspended)
                           {
                               hdd_set_pwrparams(pHddCtx);
                           }
                           hdd_enable_bmps_imps(pHddCtx);
                      }
                   }
               }
            }
        }
    }

     hdd_wmm_adapter_clear(pAdapter);
#if defined(WLAN_FEATURE_VOWIFI_11R)
    sme_FTReset(WLAN_HDD_GET_HAL_CTX(pAdapter), pAdapter->sessionId);
#endif
    if (hdd_unset_beacon_filter(pAdapter) != 0)
        hddLog(LOGE,
               FL("hdd_unset_beacon_filter() failed"));

    if (eCSR_ROAM_IBSS_LEAVE == roamStatus) {
        v_U8_t i;

        sta_id = IBSS_BROADCAST_STAID;
        vstatus = hdd_roamDeregisterSTA(pAdapter, sta_id);
        if (!VOS_IS_STATUS_SUCCESS(vstatus)) {
            hddLog(LOGE,
                   FL("hdd_roamDeregisterSTA() failed for staID %d Status=%d [0x%x]"),
                   sta_id, status, status);
            status = eHAL_STATUS_FAILURE;
        }
        pHddCtx->sta_to_adapter[sta_id] = NULL;

       /*Clear all the peer sta register with TL.*/
        for (i =0; i < HDD_MAX_NUM_IBSS_STA; i++) {
            if (0 != pHddStaCtx->conn_info.staId[i]) {
               sta_id = pHddStaCtx->conn_info.staId[i];
               VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                     FL("Deregister StaID %d"),sta_id);
               vstatus = hdd_roamDeregisterSTA( pAdapter, sta_id );
               if (!VOS_IS_STATUS_SUCCESS(vstatus)) {
                   VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      FL("hdd_roamDeregisterSTA() failed to for staID %d.  "
                      "Status= %d [0x%x]"),
                      sta_id, status, status);
                   status = eHAL_STATUS_FAILURE;
               }

               /*set the staid and peer mac as 0, all other reset are
                * done in hdd_connRemoveConnectInfo.
                */
               pHddStaCtx->conn_info.staId[i]= 0;
               vos_mem_zero(&pHddStaCtx->conn_info.peerMacAddress[i], sizeof(v_MACADDR_t));
               if (sta_id < (WLAN_MAX_STA_COUNT + 3))
                    pHddCtx->sta_to_adapter[sta_id] = NULL;
            }
        }
    } else {
       sta_id = pHddStaCtx->conn_info.staId[0];

       /* clear scan cache for Link Lost */
       if (pRoamInfo && !pRoamInfo->reasonCode &&
           (eCSR_ROAM_RESULT_DEAUTH_IND == roamResult)) {
           wlan_hdd_cfg80211_update_bss_list(pAdapter,
                               pHddStaCtx->conn_info.bssId);
           sme_remove_bssid_from_scan_list(pHddCtx->hHal,
                               pHddStaCtx->conn_info.bssId);
        }

       //We should clear all sta register with TL, for now, only one.
       vstatus = hdd_roamDeregisterSTA( pAdapter, sta_id );
       if (!VOS_IS_STATUS_SUCCESS(vstatus)) {
           VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  FL("hdd_roamDeregisterSTA() failed to for staID %d.  "
                  "Status= %d [0x%x]"),
                  sta_id, status, status);

           status = eHAL_STATUS_FAILURE;
       }
       pHddCtx->sta_to_adapter[sta_id] = NULL;
    }


    // Clear saved connection information in HDD
    hdd_connRemoveConnectInfo( pHddStaCtx );
    hdd_connSetConnectionState(pAdapter,
                            eConnectionState_NotConnected);
#ifdef WLAN_FEATURE_GTK_OFFLOAD
    if ((WLAN_HDD_INFRA_STATION == pAdapter->device_mode) ||
        (WLAN_HDD_P2P_CLIENT == pAdapter->device_mode))
    {
        memset(&pHddStaCtx->gtkOffloadReqParams, 0,
              sizeof (tSirGtkOffloadParams));
        pHddStaCtx->gtkOffloadReqParams.ulFlags = GTK_OFFLOAD_DISABLE;
    }
#endif

#ifdef FEATURE_WLAN_TDLS
    if (eCSR_ROAM_IBSS_LEAVE != roamStatus)
    {
        wlan_hdd_tdls_disconnection_callback(pAdapter);
    }
#endif

    if (pHddCtx->cfg_ini->enablePowersaveOffload &&
       ((WLAN_HDD_INFRA_STATION == pAdapter->device_mode) ||
        (WLAN_HDD_P2P_CLIENT == pAdapter->device_mode)))
    {
       sme_PsOffloadDisableDeferredPowerSave(
                   WLAN_HDD_GET_HAL_CTX(pAdapter),
                   pAdapter->sessionId);
    }

    wlan_hdd_clear_link_layer_stats(pAdapter);

    /* Decide ANTENNA_MODE on STA/CLI disconnect */
    if (pHddCtx->cfg_ini->enable_dynamic_sta_chainmask)
       hdd_decide_dynamic_chain_mask(pHddCtx,
                          HDD_ANTENNA_MODE_INVALID);

    //Unblock anyone waiting for disconnect to complete
    complete(&pAdapter->disconnect_comp_var);
    hdd_print_bss_info(pHddStaCtx);
    return( status );
}

/**
 * hdd_roamRegisterSTA() - Registers new STA to TL module
 * @pAdapter: pointer to adapter context
 * @pRoamInfo: roam info struct pointer
 * @staId: station ID
 * @pPeerMacAddress: mac address of new STA
 * @pBssDesc: bss descriptor
 *
 * Return: status of operation
 */
VOS_STATUS hdd_roamRegisterSTA(hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo,
                               uint8_t staId, v_MACADDR_t *pPeerMacAddress,
                               tSirBssDescription *pBssDesc)
{
   VOS_STATUS vosStatus = VOS_STATUS_E_FAILURE;
   WLAN_STADescType staDesc = {0};
   eCsrEncryptionType connectedCipherAlgo;
   v_BOOL_t  fConnected;
   hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
   hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);

   if ( NULL == pBssDesc)
   {
       return VOS_STATUS_E_FAILURE;
   }
   /* Get the Station ID from the one saved during the association */
   staDesc.ucSTAId = staId;

   if ( pHddStaCtx->conn_info.connDot11DesiredBssType == eMib_dot11DesiredBssType_infrastructure)
   {
      staDesc.wSTAType = WLAN_STA_INFRA;

      // grab the bssid from the connection info in the adapter structure and hand that
      // over to TL when registering.
      vos_mem_copy( staDesc.vSTAMACAddress.bytes, pHddStaCtx->conn_info.bssId,sizeof(pHddStaCtx->conn_info.bssId) );
   }
   else
   {
      // for an IBSS 'connect', setup the Station Descriptor for TL.
      staDesc.wSTAType = WLAN_STA_IBSS;

      /*
       * Note that for IBSS, the STA MAC address and BSSID are going to be
       * different where in infrastructure, they are the same (BSSID is the
       * MAC address of the AP). So, for IBSS we have a second field to pass
       * to TL in the STA descriptor that we don't pass when making an
       * Infrastructure connection.
       */
      vos_mem_copy( staDesc.vSTAMACAddress.bytes, pPeerMacAddress->bytes,sizeof(pPeerMacAddress->bytes) );
      vos_mem_copy( staDesc.vBSSIDforIBSS.bytes, pHddStaCtx->conn_info.bssId,6 );
   }

   vos_copy_macaddr( &staDesc.vSelfMACAddress, &pAdapter->macAddressCurrent );

   // set the QoS field appropriately
   if (hdd_wmm_is_active(pAdapter))
   {
      staDesc.ucQosEnabled = 1;
   }
   else
   {
      staDesc.ucQosEnabled = 0;
   }

   fConnected = hdd_connGetConnectedCipherAlgo( pHddStaCtx, &connectedCipherAlgo );
   if ( connectedCipherAlgo != eCSR_ENCRYPT_TYPE_NONE )
   {
      staDesc.ucProtectedFrame = 1;
   }
   else
   {
      staDesc.ucProtectedFrame = 0;

   }

#ifdef FEATURE_WLAN_ESE
   staDesc.ucIsEseSta = pRoamInfo->isESEAssoc;
#endif //FEATURE_WLAN_ESE

#ifdef VOLANS_ENABLE_SW_REPLAY_CHECK
   /* check whether replay check is valid for the station or not */
   if( (eCSR_ENCRYPT_TYPE_TKIP == connectedCipherAlgo) || (eCSR_ENCRYPT_TYPE_AES == connectedCipherAlgo))
   {
       /* Encryption mode is either TKIP or AES
          and replay check is valid for only these
          two encryption modes */
       staDesc.ucIsReplayCheckValid = VOS_TRUE;
       VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                 "HDD register TL ucIsReplayCheckValid %d: Replay check is needed for station", staDesc.ucIsReplayCheckValid);
   }

   else
   {
      /* For other encryption modes replay check is
         not needed */
        staDesc.ucIsReplayCheckValid = VOS_FALSE;
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                 "HDD register TL ucIsReplayCheckValid %d", staDesc.ucIsReplayCheckValid);
   }
#endif

#ifdef FEATURE_WLAN_WAPI
   hddLog(LOG1, "%s: WAPI STA Registered: %d", __func__, pAdapter->wapi_info.fIsWapiSta);
   if (pAdapter->wapi_info.fIsWapiSta)
   {
      staDesc.ucIsWapiSta = 1;
   }
   else
   {
      staDesc.ucIsWapiSta = 0;
   }
#endif /* FEATURE_WLAN_WAPI */

   VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_MED,
                 "HDD register TL Sec_enabled= %d.", staDesc.ucProtectedFrame );

   // UMA is Not ready yet, Xlation will be done by TL
   staDesc.ucSwFrameTXXlation = 1;
   staDesc.ucSwFrameRXXlation = 1;
   staDesc.ucAddRmvLLC = 1;
   VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH, "HDD register TL QoS_enabled=%d",
              staDesc.ucQosEnabled );
   // Initialize signatures and state
   staDesc.ucUcastSig  = pRoamInfo->ucastSig;
   staDesc.ucBcastSig  = pRoamInfo->bcastSig;
   staDesc.ucInitState = pRoamInfo->fAuthRequired ?
      WLANTL_STA_CONNECTED : WLANTL_STA_AUTHENTICATED;
   // Register the Station with TL...
   VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH, "%s: HDD register TL ucInitState=%d", __func__, staDesc.ucInitState );

   /* Incase Micro controller data path offload enabled,
    * All the traffic routed to WLAN host driver, do not need to
    * route IPA. It should be routed kernel network stack */
#if defined(IPA_OFFLOAD) && !defined(IPA_UC_OFFLOAD)
   if (hdd_ipa_is_enabled(pHddCtx))
      vosStatus = WLANTL_RegisterSTAClient(pHddCtx->pvosContext,
                                         hdd_ipa_process_rxt,
                                         &staDesc,
                                         pBssDesc->rssi);
   else
#endif
   vosStatus = WLANTL_RegisterSTAClient(pHddCtx->pvosContext,
                                         hdd_rx_packet_cbk,
                                         &staDesc,
                                         pBssDesc->rssi);
   if ( !VOS_IS_STATUS_SUCCESS( vosStatus ) )
   {
      VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_WARN,
                 "WLANTL_RegisterSTAClient() failed to register.  Status= %d [0x%08X]",
                 vosStatus, vosStatus );
      return vosStatus;
   }

   // if (WPA), tell TL to go to 'connected' and after keys come to the driver,
   // then go to 'authenticated'.  For all other authentication types
   // (those that donot require upper layer authentication) we can put
   // TL directly into 'authenticated' state.
   if (staDesc.wSTAType != WLAN_STA_IBSS) {
      VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_MED,
                 "STA type %d fConnected %d", staDesc.wSTAType, fConnected);
   }


   if ( !pRoamInfo->fAuthRequired )
   {
      // Connections that do not need Upper layer auth, transition TL directly
      // to 'Authenticated' state.
      vosStatus = WLANTL_ChangeSTAState(pHddCtx->pvosContext, staDesc.ucSTAId,
                                        WLANTL_STA_AUTHENTICATED,
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
                                        pRoamInfo->roamSynchInProgress
#else
                                        VOS_FALSE
#endif
                                       );

      hdd_connSetAuthenticated(pAdapter, VOS_TRUE);
   }
   else
   {
      VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_MED,
                 "ULA auth StaId= %d. Changing TL state to CONNECTED"
                 "at Join time", pHddStaCtx->conn_info.staId[0] );
      vosStatus = WLANTL_ChangeSTAState(pHddCtx->pvosContext, staDesc.ucSTAId,
                                        WLANTL_STA_CONNECTED,
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
                                        pRoamInfo->roamSynchInProgress
#else
                                        VOS_FALSE
#endif
                                       );
      hdd_connSetAuthenticated(pAdapter, VOS_FALSE);
   }
   return( vosStatus );
}

#ifdef WLAN_FEATURE_ROAM_OFFLOAD
/**
 * hdd_send_roam_auth_event() - send roamed and authed event
 * @hdd_ctx:   pointer to hdd context.
 * @bssid:    pointer to bssid of roamed AP.
 * @req_rsn_ie:    pointer to request RSN IE
 * @req_rsn_len:   length of the request RSN IE
 * @rsp_rsn_ie:    pointer to response RSN IE
 * @rsp_rsn_len:   length of the response RSN IE
 * @roam_info: pointer to the roaming related information
 *
 * this routine includes a condition check before call
 * wlan_hdd_send_roam_auth_event.
 */
static int hdd_send_roam_auth_event(hdd_context_t *hdd_ctx,
		uint8_t *bssid, uint8_t *req_rsn_ie,
		uint32_t req_rsn_len, uint8_t *rsp_rsn_ie,
		uint32_t rsp_rsn_len,
		tCsrRoamInfo *roam_info)
{
	if (hdd_ctx->cfg_ini->isRoamOffloadEnabled &&
	    roam_info->roamSynchInProgress)
		wlan_hdd_send_roam_auth_event(hdd_ctx, bssid,
					req_rsn_ie, req_rsn_len,
					rsp_rsn_ie, rsp_rsn_len,
					roam_info);
	return 0;
}
/**
 * hdd_is_roam_sync_in_progress()- Check if roam offloaded
 *
 * Return: roam sync status if roaming offloaded else false
 */
static inline bool hdd_is_roam_sync_in_progress(tCsrRoamInfo *roaminfo)
{
	return roaminfo->roamSynchInProgress;
}
#else
static inline bool hdd_is_roam_sync_in_progress(tCsrRoamInfo *roaminfo)
{
	return false;
}

static inline int hdd_send_roam_auth_event(hdd_context_t *hdd_ctx,
		uint8_t *bssid, uint8_t *req_rsn_ie, uint32_t req_rsn_length,
		uint8_t *rsp_rsn_ie, uint32_t rsp_rsn_length,
		tCsrRoamInfo *roam_info)
{
	return 0;
}
#endif

/**
 * hdd_send_roamed_ind() - send roamed indication to cfg80211
 * @dev: network device
 * @bss: cfg80211 roamed bss pointer
 * @req_ie: IEs used in reassociation request
 * @req_ie_len: Length of the @req_ie
 * @resp_ie: IEs received in successful reassociation response
 * @resp_ie_len: Length of @resp_ie
 *
 * Return: none
 */
#if defined CFG80211_ROAMED_API_UNIFIED || \
       (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0))
static void hdd_send_roamed_ind(struct net_device *dev,
				struct cfg80211_bss *bss, const uint8_t *req_ie,
				size_t req_ie_len, const uint8_t *resp_ie,
				size_t resp_ie_len)
{
	struct cfg80211_roam_info info = {0};

	info.bss = bss;
	info.req_ie = req_ie;
	info.req_ie_len = req_ie_len;
	info.resp_ie = resp_ie;
	info.resp_ie_len = resp_ie_len;
	cfg80211_roamed(dev, &info, GFP_KERNEL);
}
#else
static inline void hdd_send_roamed_ind(struct net_device *dev,
				       struct cfg80211_bss *bss,
				       const uint8_t *req_ie, size_t req_ie_len,
				       const uint8_t *resp_ie,
				       size_t resp_ie_len)
{
	cfg80211_roamed_bss(dev, bss, req_ie, req_ie_len, resp_ie, resp_ie_len,
			    GFP_KERNEL);
}
#endif

static void hdd_SendReAssocEvent(struct net_device *dev,
                                 hdd_adapter_t *pAdapter,
                                 tCsrRoamInfo *pCsrRoamInfo, v_U8_t *reqRsnIe,
                                 tANI_U32 reqRsnLength)
{
    unsigned int len = 0;
    u8 *pFTAssocRsp = NULL;
    v_U8_t *rspRsnIe = vos_mem_malloc(IW_GENERIC_IE_MAX);
    tANI_U32 rspRsnLength = 0;
    struct ieee80211_channel *chan;
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
    uint8_t buf_ssid_ie[2 + SIR_MAC_SSID_EID_MAX]; /* 2 bytes for EID and len */
    uint8_t *buf_ptr, ssid_ie_len;
    struct cfg80211_bss *bss = NULL;
    uint8_t *final_req_ie = NULL;
    tCsrRoamConnectedProfile roam_profile;
    tHalHandle hal_handle = WLAN_HDD_GET_HAL_CTX(pAdapter);

    if (!rspRsnIe) {
        hddLog(LOGE, FL("Unable to allocate RSN IE"));
        return;
    }

    if (pCsrRoamInfo == NULL) {
        hddLog(LOGE, FL("Invalid CSR roam info"));
        goto done;
    }

    if (pCsrRoamInfo->nAssocRspLength < FT_ASSOC_RSP_IES_OFFSET) {
        hddLog(LOGE, FL("Invalid assoc response length %d"),
               pCsrRoamInfo->nAssocRspLength);
        goto done;
    }

    pFTAssocRsp = (u8 *)(pCsrRoamInfo->pbFrames + pCsrRoamInfo->nBeaconLength +
                    pCsrRoamInfo->nAssocReqLength);
    if (pFTAssocRsp == NULL)
        goto done;

    /* pFTAssocRsp needs to point to the IEs */
    pFTAssocRsp += FT_ASSOC_RSP_IES_OFFSET;
    hddLog(LOG1, FL("AssocRsp is now at %02x%02x"),
                   (unsigned int)pFTAssocRsp[0], (unsigned int)pFTAssocRsp[1]);

   /* Active session count is decremented upon disconnection, but during
    * roaming, there is no disconnect indication and hence active session
    * count is not decremented.
    * After roaming is completed, active session count is incremented
    * as a part of connect indication but effectively after roaming the
    * active session count should still be the same and hence upon
    * successful reassoc decrement the active session count here */
    wlan_hdd_decr_active_session(pHddCtx, pAdapter->device_mode);

    /* Send the Assoc Resp, the supplicant needs this for initial Auth */
    len = pCsrRoamInfo->nAssocRspLength - FT_ASSOC_RSP_IES_OFFSET;
    if (len > IW_GENERIC_IE_MAX) {
        hddLog(LOGE, FL("Invalid Assoc resp length %d"), len);
        goto done;
    }
    rspRsnLength = len;
    memcpy(rspRsnIe, pFTAssocRsp, len);
    memset(rspRsnIe + len, 0, IW_GENERIC_IE_MAX - len);

    chan = ieee80211_get_channel(pAdapter->wdev.wiphy,
                                 (int)pCsrRoamInfo->pBssDesc->channelId);
    memset(&roam_profile, 0, sizeof(tCsrRoamConnectedProfile));
    sme_RoamGetConnectProfile(hal_handle, pAdapter->sessionId, &roam_profile);
    bss = hdd_cfg80211_get_bss(pAdapter->wdev.wiphy,
            chan, pCsrRoamInfo->bssid,
            &roam_profile.SSID.ssId[0],
            roam_profile.SSID.length);
    if (bss == NULL)
        hddLog(LOGE, FL("Get BSS returned NULL"));
    buf_ptr = buf_ssid_ie;
    *buf_ptr = SIR_MAC_SSID_EID;
    buf_ptr++;
    *buf_ptr = roam_profile.SSID.length; /*len of ssid*/
    buf_ptr++;
    vos_mem_copy(buf_ptr, &roam_profile.SSID.ssId[0],
    roam_profile.SSID.length);
    ssid_ie_len = 2 + roam_profile.SSID.length;
    hddLog(LOG2, FL("SSIDIE:"));
    VOS_TRACE_HEX_DUMP(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_DEBUG,
       buf_ssid_ie, ssid_ie_len);
    final_req_ie = vos_mem_malloc(IW_GENERIC_IE_MAX);
    if (final_req_ie == NULL)
        goto done;
    buf_ptr = final_req_ie;
    vos_mem_copy(buf_ptr, buf_ssid_ie, ssid_ie_len);
    buf_ptr += ssid_ie_len;
    vos_mem_copy(buf_ptr, reqRsnIe, reqRsnLength);
    memcpy(rspRsnIe, pFTAssocRsp, len);
    memset(final_req_ie + (ssid_ie_len + reqRsnLength), 0,
    IW_GENERIC_IE_MAX - (ssid_ie_len + reqRsnLength));
    hddLog(LOG2, FL("Req RSN IE:"));
    VOS_TRACE_HEX_DUMP(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_DEBUG,
       final_req_ie, (ssid_ie_len +reqRsnLength));
    hdd_send_roamed_ind(dev, bss, final_req_ie,
                        (ssid_ie_len + reqRsnLength), rspRsnIe,
                        rspRsnLength);

    hdd_send_roam_auth_event(pHddCtx, pCsrRoamInfo->bssid,
                    reqRsnIe, reqRsnLength, rspRsnIe,
                    rspRsnLength, pCsrRoamInfo);
done:
   sme_RoamFreeConnectProfile(hal_handle, &roam_profile);
   if (final_req_ie)
      vos_mem_free(final_req_ie);
   vos_mem_free(rspRsnIe);
}


/**
 * hdd_change_sta_state_authenticated()-
 * This function changes STA state to authenticated
 * @adapter:  pointer to the adapter structure.
 * @roaminfo: pointer to the RoamInfo structure.
 *
 * This is called from hdd_RoamSetKeyCompleteHandler
 * in context to eCSR_ROAM_SET_KEY_COMPLETE event from fw.
 *
 * Return: 0 on success and errno on failure
 */
static int hdd_change_sta_state_authenticated(hdd_adapter_t *adapter,
						 tCsrRoamInfo *roaminfo)
{
	int ret;
	hdd_context_t *hddctx = WLAN_HDD_GET_CTX(adapter);
	hdd_station_ctx_t *hddstactx = WLAN_HDD_GET_STATION_CTX_PTR(adapter);

	hddLog(LOG1,
		"Changing TL state to AUTHENTICATED for StaId= %d",
		hddstactx->conn_info.staId[0]);

	/* Connections that do not need Upper layer authentication,
	 * transition TL to 'Authenticated' state after the keys are set
	 */
	ret = WLANTL_ChangeSTAState(hddctx->pvosContext,
			hddstactx->conn_info.staId[0],
			WLANTL_STA_AUTHENTICATED,
			hdd_is_roam_sync_in_progress(roaminfo));
	hdd_connSetAuthenticated(adapter, VOS_TRUE);
	if (hddctx->cfg_ini->enablePowersaveOffload &&
		(false == hddctx->is_mon_enable) &&
		((WLAN_HDD_INFRA_STATION == adapter->device_mode) ||
		(WLAN_HDD_P2P_CLIENT == adapter->device_mode))) {
		sme_PsOffloadEnableDeferredPowerSave(
			WLAN_HDD_GET_HAL_CTX(adapter),
			adapter->sessionId,
			hddstactx->hdd_ReassocScenario);
	}

	return ret;
}

void hdd_PerformRoamSetKeyComplete(hdd_adapter_t *pAdapter)
{
	eHalStatus halStatus = eHAL_STATUS_SUCCESS;
	hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
	tCsrRoamInfo *roam_info;

	roam_info = vos_mem_malloc(sizeof(*roam_info));
	if (!roam_info)
		return;

	vos_mem_set(roam_info, sizeof(*roam_info), 0);
	roam_info->fAuthRequired = FALSE;
	vos_mem_copy(roam_info->bssid,
	             pHddStaCtx->roam_info.bssid,
	             VOS_MAC_ADDR_SIZE);
	vos_mem_copy(roam_info->peerMac,
	             pHddStaCtx->roam_info.peerMac,
	             VOS_MAC_ADDR_SIZE);

	halStatus = hdd_RoamSetKeyCompleteHandler(
			pAdapter,
			roam_info,
			pHddStaCtx->roam_info.roamId,
			pHddStaCtx->roam_info.roamStatus,
			eCSR_ROAM_RESULT_AUTHENTICATED);
	if (halStatus != eHAL_STATUS_SUCCESS)
		hddLog(LOGE, "%s: Set Key complete failure", __func__);
	pHddStaCtx->roam_info.deferKeyComplete = FALSE;
	vos_mem_free(roam_info);
}

#if defined(WLAN_FEATURE_FILS_SK) && defined(CFG80211_FILS_SK_OFFLOAD_SUPPORT)
static void hdd_clear_fils_connection_info(hdd_adapter_t *adapter)
{
       hdd_wext_state_t *wext_state = WLAN_HDD_GET_WEXT_STATE_PTR(adapter);

       if (wext_state->roamProfile.fils_con_info) {
              vos_mem_free(wext_state->roamProfile.fils_con_info);
              wext_state->roamProfile.fils_con_info = NULL;
       }
}
#else
static void hdd_clear_fils_connection_info(hdd_adapter_t *adapter)
{ }
#endif

/**
 * hdd_sap_restart_handle() - to handle restarting of SAP
 * @work: name of the work
 *
 * Purpose of this function is to trigger sap start. this function
 * will be called from workqueue.
 *
 * Return: void.
 */
void hdd_sap_restart_handle(struct work_struct *work)
{
    hdd_adapter_t *sap_adapter;
    hdd_context_t *hdd_ctx = container_of(work,
                                          hdd_context_t,
                                          sap_start_work);
    vos_ssr_protect(__func__);
    if (0 != wlan_hdd_validate_context(hdd_ctx)) {
        vos_ssr_unprotect(__func__);
        return;
    }
    sap_adapter = hdd_get_adapter(hdd_ctx,
                                  WLAN_HDD_SOFTAP);
    if (sap_adapter == NULL) {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  FL("sap_adapter is NULL"));
        vos_ssr_unprotect(__func__);
        return;
    }

    if (hdd_ctx->is_ch_avoid_in_progress) {
        sap_adapter->sessionCtx.ap.sapConfig.channel = AUTO_CHANNEL_SELECT;
        wlan_hdd_restart_sap(sap_adapter);
        hdd_change_ch_avoidance_status(hdd_ctx, false);
    } else {
        wlan_hdd_start_sap(sap_adapter, false);
        hdd_change_sap_restart_required_status(hdd_ctx, false);
    }
    vos_ssr_unprotect(__func__);
}

static eHalStatus hdd_AssociationCompletionHandler( hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo,
                                                    tANI_U32 roamId, eRoamCmdStatus roamStatus,
                                                    eCsrRoamResult roamResult )
{
    struct net_device *dev = pAdapter->dev;
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
    hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
#ifdef FEATURE_WLAN_FORCE_SAP_SCC
    hdd_adapter_t *pHostapdAdapter;
#endif /* FEATURE_WLAN_FORCE_SAP_SCC */
    VOS_STATUS vosStatus = VOS_STATUS_E_FAILURE;
    v_U8_t reqRsnIe[DOT11F_IE_RSN_MAX_LEN];
    tANI_U32 reqRsnLength = DOT11F_IE_RSN_MAX_LEN;
#if  defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR) || defined (WLAN_FEATURE_VOWIFI_11R)
    int ft_carrier_on = FALSE;
#endif
    v_BOOL_t hddDisconInProgress = FALSE;
    tHalHandle hHal = WLAN_HDD_GET_HAL_CTX(pAdapter);
    tpAniSirGlobal pMac = PMAC_STRUCT(hHal);
    unsigned long rc;
    hdd_adapter_t *sap_adapter;
    hdd_ap_ctx_t *hdd_ap_ctx;
    uint8_t default_sap_channel = 6;
    tSirResultCodes timeout_reason = 0;
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
    if (pRoamInfo && pRoamInfo->roamSynchInProgress) {
       /* change logging before release */
        hddLog(VOS_TRACE_LEVEL_DEBUG, "LFR3:hdd_AssociationCompletionHandler");
    }
#endif

    /* HDD has initiated disconnect, do not send connect result indication
     * to kernel as it will be handled by __cfg80211_disconnect.
     */
    if(((eConnectionState_Disconnecting == pHddStaCtx->conn_info.connState) ||
         (eConnectionState_NotConnected == pHddStaCtx->conn_info.connState)) &&
        ((eCSR_ROAM_RESULT_ASSOCIATED == roamResult) ||
        (eCSR_ROAM_ASSOCIATION_FAILURE == roamStatus)))
    {
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                FL(" Disconnect from HDD in progress "));
        hddDisconInProgress  = TRUE;
    }

    if ( eCSR_ROAM_RESULT_ASSOCIATED == roamResult )
    {
        if (NULL == pRoamInfo) {
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      FL("pRoamInfo is NULL"));
            return eHAL_STATUS_FAILURE;
        }
        if ( !hddDisconInProgress )
        {
            hdd_connSetConnectionState(pAdapter,
                                       eConnectionState_Associated);
        }

        // Save the connection info from CSR...
        hdd_connSaveConnectInfo( pAdapter, pRoamInfo, eCSR_BSS_TYPE_INFRASTRUCTURE );

        if (hdd_set_beacon_filter(pAdapter) != 0)
            hddLog(LOGE,
                   FL("hdd_set_beacon_filter() failed"));

#ifdef FEATURE_WLAN_WAPI
        if ( pRoamInfo->u.pConnectedProfile->AuthType == eCSR_AUTH_TYPE_WAPI_WAI_CERTIFICATE ||
                pRoamInfo->u.pConnectedProfile->AuthType == eCSR_AUTH_TYPE_WAPI_WAI_PSK )
        {
            pAdapter->wapi_info.fIsWapiSta = 1;
        }
        else
        {
            pAdapter->wapi_info.fIsWapiSta = 0;
        }
#endif  /* FEATURE_WLAN_WAPI */

        /* Indicate 'connect' status to user space */
        hdd_SendAssociationEvent(dev,pRoamInfo);

	if (hdd_is_mcc_in_24G(pHddCtx)) {
		if ((pMac != NULL) && (pHddCtx->miracast_value)) {
			hdd_set_mas(pAdapter, pHddCtx->miracast_value);
		}
	}

        // Initialize the Linkup event completion variable
        INIT_COMPLETION(pAdapter->linkup_event_var);

        /*
           Sometimes Switching ON the Carrier is taking time to activate the device properly. Before allowing any
           packet to go up to the application, device activation has to be ensured for proper queue mapping by the
           kernel. we have registered net device notifier for device change notification. With this we will come to
           know that the device is getting activated properly.
           */
#if  defined (WLAN_FEATURE_VOWIFI_11R) || defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR)
        if (pHddStaCtx->ft_carrier_on == FALSE)
        {
#endif
            // Enable Linkup Event Servicing which allows the net device notifier to set the linkup event variable
            pAdapter->isLinkUpSvcNeeded = TRUE;

            // Enable Linkup Event Servicing which allows the net device notifier to set the linkup event variable
            pAdapter->isLinkUpSvcNeeded = TRUE;

            // Switch on the Carrier to activate the device
            wlan_hdd_netif_queue_control(pAdapter, WLAN_NETIF_CARRIER_ON,
                             WLAN_CONTROL_PATH);

            // Wait for the Link to up to ensure all the queues are set properly by the kernel
            rc = wait_for_completion_timeout(&pAdapter->linkup_event_var,
                                                   msecs_to_jiffies(ASSOC_LINKUP_TIMEOUT));
            if (!rc) {
                hddLog(VOS_TRACE_LEVEL_WARN, "%s: Warning:ASSOC_LINKUP_TIMEOUT", __func__);
            }

            // Disable Linkup Event Servicing - no more service required from the net device notifier call
            pAdapter->isLinkUpSvcNeeded = FALSE;
#if  defined (WLAN_FEATURE_VOWIFI_11R) || defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR)
        }
        else {
            pHddStaCtx->ft_carrier_on = FALSE;
            ft_carrier_on = TRUE;
        }
#endif
        /* Check for STAID */
        if ((WLAN_MAX_STA_COUNT + 3) > pRoamInfo->staId)
           pHddCtx->sta_to_adapter[pRoamInfo->staId] = pAdapter;
        else
           hddLog(VOS_TRACE_LEVEL_ERROR, "%s: Wrong Staid: %d", __func__,
                                                       pRoamInfo->staId);

#ifdef IPA_OFFLOAD
        if (hdd_ipa_is_enabled(pHddCtx))
            hdd_ipa_wlan_evt(pAdapter, pRoamInfo->staId, WLAN_STA_CONNECT,
                    pRoamInfo->bssid);
#endif
#ifdef FEATURE_WLAN_AUTO_SHUTDOWN
        wlan_hdd_auto_shutdown_enable(pHddCtx, VOS_FALSE);
#endif
#ifdef FEATURE_WLAN_DISABLE_CHANNEL_SWITCH
        if (!vos_is_chan_ok_for_dnbs(pHddStaCtx->conn_info.operationChannel)) {
            hddLog(VOS_TRACE_LEVEL_ERROR,
                          "%s: chan-%d is not suitable for DNBS", __func__, pHddStaCtx->conn_info.operationChannel);
            wlan_hdd_netif_queue_control(pAdapter, WLAN_NETIF_CARRIER_OFF, WLAN_CONTROL_PATH);
            if (!hddDisconInProgress) {
                hddLog(VOS_TRACE_LEVEL_ERROR, "%s: Disconnecting...", __func__);
                sme_RoamDisconnect(WLAN_HDD_GET_HAL_CTX(pAdapter),
                                    pAdapter->sessionId,
                                    eCSR_DISCONNECT_REASON_UNSPECIFIED);
            }
            return eHAL_STATUS_FAILURE;
        }
#endif
        /* validate cfg_ini */
        if (!pHddCtx->cfg_ini) {
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                          "cfg_ini is NULL");
                return eHAL_STATUS_E_NULL_VALUE;
        }
#ifdef FEATURE_WLAN_MCC_TO_SCC_SWITCH
        if ((pHddCtx->cfg_ini->WlanMccToSccSwitchMode
                != VOS_MCC_TO_SCC_SWITCH_DISABLE) &&
            ((0 == pHddCtx->cfg_ini->conc_custom_rule1) &&
             (0 == pHddCtx->cfg_ini->conc_custom_rule2))
#ifdef FEATURE_WLAN_STA_AP_MODE_DFS_DISABLE
            && !VOS_IS_DFS_CH(pHddStaCtx->conn_info.operationChannel)
#endif
           ) {
            adf_os_create_work(0, &pHddCtx->sta_ap_intf_check_work,
                wlan_hdd_check_sta_ap_concurrent_ch_intf, (void *)pAdapter);
            adf_os_sched_work(0, &pHddCtx->sta_ap_intf_check_work);
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                                "Checking for Concurrent Change interference");
        }
#endif

#ifdef QCA_PKT_PROTO_TRACE
        /* STA Associated, update into trace buffer */
        if (pHddCtx->cfg_ini->gEnableDebugLog)
        {
           vos_pkt_trace_buf_update("ST:ASSOC");
        }
#endif /* QCA_PKT_PROTO_TRACE */

        DPTRACE(adf_dp_trace_mgmt_pkt(ADF_DP_TRACE_MGMT_PACKET_RECORD,
                       pAdapter->sessionId,
                       ADF_PROTO_TYPE_MGMT, ADF_PROTO_MGMT_ASSOC));

        //For reassoc, the station is already registered, all we need is to change the state
        //of the STA in TL.
        //If authentication is required (WPA/WPA2/DWEP), change TL to CONNECTED instead of AUTHENTICATED
        if( !pRoamInfo->fReassocReq )
        {
            struct cfg80211_bss *bss;
#ifdef WLAN_FEATURE_VOWIFI_11R
            u8 *pFTAssocRsp = NULL;
            unsigned int assocRsplen = 0;
            u8 *pFTAssocReq = NULL;
            unsigned int assocReqlen = 0;
            struct ieee80211_channel *chan;
#endif
            v_U8_t rspRsnIe[DOT11F_IE_RSN_MAX_LEN];
            tANI_U32 rspRsnLength = DOT11F_IE_RSN_MAX_LEN;

            /* add bss_id to cfg80211 data base */
            bss = wlan_hdd_cfg80211_update_bss_db(pAdapter, pRoamInfo);
            if (NULL == bss) {
                hddLog(LOGE,
                     FL("Not able to add BSS entry"));
                wlan_hdd_netif_queue_control(pAdapter,
                    WLAN_NETIF_CARRIER_OFF,
                    WLAN_CONTROL_PATH);
                if (!hddDisconInProgress) {
                   /*
                    * Here driver was not able to add bss in cfg80211 database
                    * this can happen if connected channel is not valid,
                    * i.e reg domain was changed during connection.
                    * Queue disconnect for the session if disconnect is
                    * not in progress.
                    */
                    hddLog(LOGE, FL("Disconnecting..."));
                    sme_RoamDisconnect( WLAN_HDD_GET_HAL_CTX(pAdapter),
                                 pAdapter->sessionId,
                                 eCSR_DISCONNECT_REASON_UNSPECIFIED);
                }
                return eHAL_STATUS_FAILURE;
            }
            //Association Response
            pFTAssocRsp = (u8 *)(pRoamInfo->pbFrames + pRoamInfo->nBeaconLength +
                                    pRoamInfo->nAssocReqLength);
            if (pFTAssocRsp != NULL)
            {
                // pFTAssocRsp needs to point to the IEs
                pFTAssocRsp += FT_ASSOC_RSP_IES_OFFSET;
                hddLog(LOG1, "%s: AssocRsp is now at %02x%02x", __func__,
                                    (unsigned int)pFTAssocRsp[0],
                                    (unsigned int)pFTAssocRsp[1]);
                assocRsplen = pRoamInfo->nAssocRspLength - FT_ASSOC_RSP_IES_OFFSET;
            }
            else
            {
                hddLog(LOGE, "%s:AssocRsp is NULL", __func__);
                assocRsplen = 0;
            }

            //Association Request
            pFTAssocReq = (u8 *)(pRoamInfo->pbFrames +
                                 pRoamInfo->nBeaconLength);
            if (pFTAssocReq != NULL)
            {
                if(!ft_carrier_on)
                {
                     // pFTAssocReq needs to point to the IEs
                    pFTAssocReq += FT_ASSOC_REQ_IES_OFFSET;
                    hddLog(LOG1, "%s: pFTAssocReq is now at %02x%02x", __func__,
                                          (unsigned int)pFTAssocReq[0],
                                          (unsigned int)pFTAssocReq[1]);
                    assocReqlen = pRoamInfo->nAssocReqLength - FT_ASSOC_REQ_IES_OFFSET;
                }
                else
                {
                    /* This should contain only the FTIEs */
                    assocReqlen = pRoamInfo->nAssocReqLength;
                }
                hddLog(LOG1, "assocReqlen %d assocRsplen %d", assocReqlen,
                                         assocRsplen);
            }
            else
            {
                hddLog(LOGE, "%s:AssocReq is NULL", __func__);
                assocReqlen = 0;
            }

#ifdef WLAN_FEATURE_VOWIFI_11R
            if(pRoamInfo->u.pConnectedProfile->AuthType == eCSR_AUTH_TYPE_FT_RSN ||
                pRoamInfo->u.pConnectedProfile->AuthType == eCSR_AUTH_TYPE_FT_RSN_PSK )
            {

                if(ft_carrier_on)
                {
                    if ( !hddDisconInProgress )
                    {
                        struct cfg80211_bss *roam_bss;

                        /* After roaming is completed, active session count is
                         * incremented as a part of connect indication but
                         * effectively the active session count should still
                         * be the same and hence upon successful reassoc
                         * decrement the active session count here */
                        wlan_hdd_decr_active_session(pHddCtx,
                                                     pAdapter->device_mode);

                        hddLog(LOG1, "%s ft_carrier_on is %d, sending roamed "
                                     "indication", __FUNCTION__, ft_carrier_on);
                        chan = ieee80211_get_channel(pAdapter->wdev.wiphy,
                                             (int)pRoamInfo->pBssDesc->channelId);
                        roam_bss =
                            hdd_cfg80211_get_bss(
                               pAdapter->wdev.wiphy,
                               chan,
                               pRoamInfo->bssid,
                               pRoamInfo->u.
                               pConnectedProfile->SSID.ssId,
                               pRoamInfo->u.
                               pConnectedProfile->SSID.length);
                        hdd_send_roamed_ind(
                               dev,
                               roam_bss,
                               pFTAssocReq,
                               assocReqlen,
                               pFTAssocRsp,
                               assocRsplen);
                    }
                    if (sme_GetFTPTKState(WLAN_HDD_GET_HAL_CTX(pAdapter),
                                          pAdapter->sessionId))
                    {
                        sme_SetFTPTKState(WLAN_HDD_GET_HAL_CTX(pAdapter),
                                          pAdapter->sessionId, FALSE);
                        pRoamInfo->fAuthRequired = FALSE;

                        vos_mem_copy(pHddStaCtx->roam_info.bssid,
                                     pRoamInfo->bssid,
                                     HDD_MAC_ADDR_LEN);
                        vos_mem_copy(pHddStaCtx->roam_info.peerMac,
                                     pRoamInfo->peerMac,
                                     HDD_MAC_ADDR_LEN);
                        pHddStaCtx->roam_info.roamId = roamId;
                        pHddStaCtx->roam_info.roamStatus = roamStatus;
                        pHddStaCtx->roam_info.deferKeyComplete = TRUE;
                    }
                }
                else if ( !hddDisconInProgress )
                {
                    hddLog(LOG1, "%s ft_carrier_on is %d, sending connect "
                                 "indication", __FUNCTION__, ft_carrier_on);

                    hdd_connect_result(dev, pRoamInfo->bssid, pRoamInfo,
                                       pFTAssocReq, assocReqlen,
                                       pFTAssocRsp, assocRsplen,
                                       WLAN_STATUS_SUCCESS,
                                       GFP_KERNEL, false,
                                       pRoamInfo->statusCode);
                }
            }
            else
#endif
            {
                /* wpa supplicant expecting WPA/RSN IE in connect result */
                csrRoamGetWpaRsnReqIE(WLAN_HDD_GET_HAL_CTX(pAdapter),
                        pAdapter->sessionId,
                        &reqRsnLength,
                        reqRsnIe);

                csrRoamGetWpaRsnRspIE(WLAN_HDD_GET_HAL_CTX(pAdapter),
                        pAdapter->sessionId,
                        &rspRsnLength,
                        rspRsnIe);
                if ( !hddDisconInProgress )
                {
#if  defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR)
                    if(ft_carrier_on)
                        hdd_SendReAssocEvent(dev, pAdapter, pRoamInfo, reqRsnIe, reqRsnLength);
                    else
#endif /* FEATURE_WLAN_ESE */

                    {
                        hddLog(VOS_TRACE_LEVEL_INFO,
                               "%s: sending connect indication to nl80211:for bssid " MAC_ADDRESS_STR " result:%d and Status:%d",
                               __func__, MAC_ADDR_ARRAY(pRoamInfo->bssid),
                               roamResult, roamStatus);

                        /* inform connect result to nl80211 */
                        hdd_connect_result(dev, pRoamInfo->bssid, pRoamInfo,
                                pFTAssocReq, assocReqlen,
                                pFTAssocRsp, assocRsplen,
                                WLAN_STATUS_SUCCESS,
                                GFP_KERNEL, false, pRoamInfo->statusCode);
                    }
                }
            }
            if ( !hddDisconInProgress )
            {
                cfg80211_put_bss(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)) || defined(WITH_BACKPORTS)
                                 pHddCtx->wiphy,
#endif
                                 bss);

                // perform any WMM-related association processing
                hdd_wmm_assoc(pAdapter, pRoamInfo, eCSR_BSS_TYPE_INFRASTRUCTURE);

                /* Start the Queue - Start tx queues before hdd_roamRegisterSTA,
                   since hdd_roamRegisterSTA will flush any cached data frames
                   immediately */
                hddLog(LOG1, FL("Enabling queues"));
                wlan_hdd_netif_queue_control(pAdapter,
                    WLAN_WAKE_ALL_NETIF_QUEUE,
                    WLAN_CONTROL_PATH);

                // Register the Station with TL after associated...
                vosStatus = hdd_roamRegisterSTA( pAdapter,
                        pRoamInfo,
                        pHddStaCtx->conn_info.staId[ 0 ],
                        NULL,
                        pRoamInfo->pBssDesc );
            }

#ifdef FEATURE_WLAN_TDLS
        wlan_hdd_tdls_connection_callback(pAdapter);
#endif
        }
        else
        {
            /* wpa supplicant expecting WPA/RSN IE in connect result */
            /*  in case of reassociation also need to indicate it to supplicant */
            csrRoamGetWpaRsnReqIE(WLAN_HDD_GET_HAL_CTX(pAdapter),
                    pAdapter->sessionId,
                    &reqRsnLength,
                    reqRsnIe);

            hdd_SendReAssocEvent(dev, pAdapter, pRoamInfo, reqRsnIe, reqRsnLength);
            //Reassoc successfully
            if( pRoamInfo->fAuthRequired )
            {
                vosStatus = WLANTL_ChangeSTAState(pHddCtx->pvosContext,
                                                  pHddStaCtx->conn_info.staId[0],
                                                  WLANTL_STA_CONNECTED,
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
                                                  pRoamInfo->roamSynchInProgress
#else
                                                  VOS_FALSE
#endif
                                                 );
                hdd_connSetAuthenticated(pAdapter, VOS_FALSE);
            }
            else
            {
                VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
                          "%s: staId: %d Changing TL state to AUTHENTICATED",
                          __func__, pHddStaCtx->conn_info.staId[ 0 ] );
                vosStatus = WLANTL_ChangeSTAState(pHddCtx->pvosContext,
                                                  pHddStaCtx->conn_info.staId[0],
                                                  WLANTL_STA_AUTHENTICATED,
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
                                                  pRoamInfo->roamSynchInProgress
#else
                                                  VOS_FALSE
#endif
                                                 );
                hdd_connSetAuthenticated(pAdapter, VOS_TRUE);
            }

            if ( VOS_IS_STATUS_SUCCESS( vosStatus ) )
            {
                // perform any WMM-related association processing
                hdd_wmm_assoc(pAdapter, pRoamInfo, eCSR_BSS_TYPE_INFRASTRUCTURE);
            }

            /* Start the tx queues */
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
            if (pRoamInfo->roamSynchInProgress) {
               hddLog(VOS_TRACE_LEVEL_DEBUG, "LFR3:netif_tx_wake_all_queues");
            }
#endif
            hddLog(LOG1, FL("Enabling queues"));
            wlan_hdd_netif_queue_control(pAdapter,
                WLAN_WAKE_ALL_NETIF_QUEUE,
                WLAN_CONTROL_PATH);
        }

        if ( !VOS_IS_STATUS_SUCCESS( vosStatus ) )
        {
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                    "Cannot register STA with TL.  Failed with vosStatus = %d [%08X]",
                    vosStatus, vosStatus );
        }
#ifdef WLAN_FEATURE_11W
        vos_mem_zero( &pAdapter->hdd_stats.hddPmfStats,
                      sizeof(pAdapter->hdd_stats.hddPmfStats) );
#endif
    }
    else
    {
        hdd_context_t* pHddCtx = (hdd_context_t*)pAdapter->pHddCtx;
        bool connect_timeout = false;

        hdd_wext_state_t *pWextState = WLAN_HDD_GET_WEXT_STATE_PTR(pAdapter);
        if (pRoamInfo)
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                    "wlan: connection failed with " MAC_ADDRESS_STR " result:%d and Status:%d",
                    MAC_ADDR_ARRAY(pRoamInfo->bssid), roamResult, roamStatus);
        else
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                    "wlan: connection failed with " MAC_ADDRESS_STR " result:%d and Status:%d",
                    MAC_ADDR_ARRAY(pWextState->req_bssId),
                    roamResult, roamStatus);

        /* Set connection state to eConnectionState_NotConnected only when CSR
         * has completed operation - with a ASSOCIATION_FAILURE status
         */
        if ( eCSR_ROAM_ASSOCIATION_FAILURE == roamStatus &&  !hddDisconInProgress )
        {
            hdd_connSetConnectionState(pAdapter,
                                             eConnectionState_NotConnected);
        }
        if((pHddCtx->concurrency_mode <= 1) &&
           (pHddCtx->no_of_open_sessions[WLAN_HDD_INFRA_STATION] <=1))
        {
            pHddCtx->isAmpAllowed = VOS_TRUE;
        }

        //If the Device Mode is Station
        // and the P2P Client is Connected
        //Enable BMPS

        /*
         * In case of JB, as Change-Iface may or may not be called for p2p0
         * Enable BMPS/IMPS in case P2P_CLIENT disconnected
         * If ps offload is enabled, fw will take care in case of concurrency.
         */
        if(((WLAN_HDD_INFRA_STATION == pAdapter->device_mode) ||
            (WLAN_HDD_P2P_CLIENT == pAdapter->device_mode)) &&
            (vos_concurrent_open_sessions_running()) &&
            !pHddCtx->cfg_ini->enablePowersaveOffload)
        {
           //Enable BMPS only of other Session is P2P Client
           hdd_context_t *pHddCtx = NULL;
           v_CONTEXT_t pVosContext = vos_get_global_context( VOS_MODULE_ID_HDD, NULL );

           if (NULL != pVosContext)
           {
               pHddCtx = vos_get_context( VOS_MODULE_ID_HDD, pVosContext);

               if(NULL != pHddCtx)
               {
                    //Only P2P Client is there Enable Bmps back
                    if((0 == pHddCtx->no_of_open_sessions[VOS_STA_SAP_MODE]) &&
                       (0 == pHddCtx->no_of_open_sessions[VOS_P2P_GO_MODE]))
                    {
                         if (pHddCtx->hdd_wlan_suspended)
                         {
                             hdd_set_pwrparams(pHddCtx);
                         }
                         hdd_enable_bmps_imps(pHddCtx);
                    }
               }
           }
        }

        if ((eCSR_ROAM_RESULT_SCAN_FOR_SSID_FAILURE == roamResult) ||
           (pRoamInfo &&
           ((eSIR_SME_JOIN_TIMEOUT_RESULT_CODE == pRoamInfo->statusCode) ||
           (eSIR_SME_AUTH_TIMEOUT_RESULT_CODE == pRoamInfo->statusCode) ||
           (eSIR_SME_ASSOC_TIMEOUT_RESULT_CODE == pRoamInfo->statusCode)))) {
             wlan_hdd_cfg80211_update_bss_list(pAdapter,
                    pRoamInfo ? pRoamInfo->bssid : pWextState->req_bssId);
             sme_remove_bssid_from_scan_list(hHal,
                    pRoamInfo ? pRoamInfo->bssid : pWextState->req_bssId);
             connect_timeout = true;
        }

        /* CR465478: Only send up a connection failure result when CSR has
         * completed operation - with a ASSOCIATION_FAILURE status.*/
        if ( eCSR_ROAM_ASSOCIATION_FAILURE == roamStatus &&  !hddDisconInProgress )
        {
            if (pRoamInfo) {
                hddLog(VOS_TRACE_LEVEL_ERROR,
                       "%s: send connect failure to nl80211: for bssid " MAC_ADDRESS_STR" result:%d and Status:%d reasonCode %d" ,
                       __func__, MAC_ADDR_ARRAY(pRoamInfo->bssid),
                       roamResult, roamStatus, pRoamInfo->reasonCode);
                pHddStaCtx->conn_info.assoc_status_code = pRoamInfo->statusCode;
            } else {
                hddLog(VOS_TRACE_LEVEL_ERROR,
                       "%s: connect failed: for bssid " MAC_ADDRESS_STR " result:%d and Status:%d " ,
                       __func__, MAC_ADDR_ARRAY(pWextState->req_bssId),
                       roamResult, roamStatus);
            }

            hddLog(LOG1, FL("Invoking packetdump deregistration API"));
            wlan_deregister_txrx_packetdump();

            /* inform association failure event to nl80211 */
            if ( eCSR_ROAM_RESULT_ASSOC_FAIL_CON_CHANNEL == roamResult )
            {
               if (pRoamInfo)
                   hdd_connect_result(dev, pRoamInfo->bssid,
                        pRoamInfo,
                        NULL, 0, NULL, 0,
                        WLAN_STATUS_ASSOC_DENIED_UNSPEC,
                        GFP_KERNEL, connect_timeout, pRoamInfo->statusCode);
               else
                   hdd_connect_result(dev, pWextState->req_bssId, NULL,
                        NULL, 0, NULL, 0,
                        WLAN_STATUS_ASSOC_DENIED_UNSPEC,
                        GFP_KERNEL, connect_timeout, timeout_reason);
            }
            else
            {
               if (pRoamInfo)
                   hdd_connect_result(dev, pRoamInfo->bssid,
                        pRoamInfo,
                        NULL, 0, NULL, 0,
                        pRoamInfo->reasonCode ?
                        pRoamInfo->reasonCode :
                        WLAN_STATUS_UNSPECIFIED_FAILURE,
                        GFP_KERNEL, connect_timeout, pRoamInfo->statusCode);
               else
                   hdd_connect_result(dev, pWextState->req_bssId, NULL,
                        NULL, 0, NULL, 0,
                        WLAN_STATUS_UNSPECIFIED_FAILURE,
                        GFP_KERNEL, connect_timeout, timeout_reason);
            }
            /* Clear the roam profile */
            hdd_clearRoamProfileIe(pAdapter);

        }

        hdd_wmm_init( pAdapter );

        hddLog(LOG1, FL("Disabling queues"));
        wlan_hdd_netif_queue_control(pAdapter,
            WLAN_NETIF_TX_DISABLE_N_CARRIER,
            WLAN_CONTROL_PATH);

    }

    if (pHddCtx->cfg_ini->conc_custom_rule1 &&
        (true == hdd_is_sap_restart_required(pHddCtx))) {
        sap_adapter = hdd_get_adapter(pHddCtx, WLAN_HDD_SOFTAP);
        if (sap_adapter == NULL) {
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      FL("sap_adapter is NULL"));
            return eHAL_STATUS_FAILURE;
        }

        if (test_bit(SOFTAP_BSS_STARTED, &sap_adapter->event_flags)) {
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      FL("Oops SAP is already in started state"));
            return eHAL_STATUS_FAILURE;
        }

        hdd_ap_ctx = WLAN_HDD_GET_AP_CTX_PTR(sap_adapter);
        if ((eCSR_ROAM_RESULT_ASSOCIATED == roamResult) &&
             pHddStaCtx->conn_info.operationChannel < SIR_11A_CHANNEL_BEGIN) {
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  FL("Starting SAP on channel [%d] after STA assoc complete"),
                  pHddStaCtx->conn_info.operationChannel);
            hdd_ap_ctx->operatingChannel =
                    pHddStaCtx->conn_info.operationChannel;
        } else {
            /* start on default SAP channel */
            hdd_ap_ctx->operatingChannel =
                    default_sap_channel;
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      FL("Starting SAP on channel [%d] after STA assoc failed"),
                      default_sap_channel);
        }
        hdd_ap_ctx->sapConfig.vht_channel_width =
                                            hdd_ap_ctx->sapConfig.ch_width_orig;
        sme_SelectCBMode(WLAN_HDD_GET_HAL_CTX(sap_adapter),
                         hdd_ap_ctx->sapConfig.SapHw_mode,
                         hdd_ap_ctx->operatingChannel,
                         hdd_ap_ctx->sapConfig.sec_ch,
                         &hdd_ap_ctx->sapConfig.vht_channel_width,
                         hdd_ap_ctx->sapConfig.ch_width_orig);
        /*
         * Create a workqueue and let the workqueue handle the restarting
         * sap task. if we directly call sap restart function without
         * creating workqueue then our main thread might go to sleep which
         * is not acceptable.
         */

         /*
          * If channel avoidance is intiated, don't schedule the work.
          * Channel avoidance takes care restarting SAP.
          */
         if (true == hdd_is_sap_restart_required(pHddCtx))
             schedule_work(&pHddCtx->sap_start_work);


    }
    hdd_clear_fils_connection_info(pAdapter);
    /*
     * Call hdd_decide_dynamic_chain_mask only when CSR has
     * completed connect with failure or success i.e. with
     * ASSOCIATION_FAILURE status or with eCSR_ROAM_RESULT_ASSOCIATED
     * result.
     */
    if (pHddCtx->cfg_ini->enable_dynamic_sta_chainmask &&
       ((eCSR_ROAM_ASSOCIATION_FAILURE == roamStatus) ||
       (eCSR_ROAM_RESULT_ASSOCIATED == roamResult)))
         hdd_decide_dynamic_chain_mask(pHddCtx,
                         HDD_ANTENNA_MODE_INVALID);

#ifdef FEATURE_WLAN_FORCE_SAP_SCC
    if (eCSR_ROAM_RESULT_ASSOCIATED == roamResult &&
        pHddCtx->cfg_ini->SapSccChanAvoidance) {
        pHostapdAdapter = hdd_get_adapter(pHddCtx, WLAN_HDD_SOFTAP);
        if (pHostapdAdapter != NULL) {
             /* Restart SAP if its operating channel is different
              * from AP channel.
              */
             if (pHostapdAdapter->sessionCtx.ap.operatingChannel !=
                pRoamInfo->pBssDesc->channelId) {
                hddLog(VOS_TRACE_LEVEL_ERROR,
                       "Restart Sap as SAP channel is %d and STA channel is %d",
                       pHostapdAdapter->sessionCtx.ap.operatingChannel,
                       pRoamInfo->pBssDesc->channelId);
                hdd_restart_softap(pHddCtx, pHostapdAdapter);
             }
        }
    }
#endif /* FEATURE_WLAN_FORCE_SAP_SCC */
    return eHAL_STATUS_SUCCESS;
}

/**============================================================================
 *
  @brief hdd_RoamIbssIndicationHandler() - Here we update the status of the
  Ibss when we receive information that we have started/joined an ibss session

  ===========================================================================*/
static void hdd_RoamIbssIndicationHandler( hdd_adapter_t *pAdapter,
                                           tCsrRoamInfo *pRoamInfo,
                                           tANI_U32 roamId,
                                           eRoamCmdStatus roamStatus,
                                           eCsrRoamResult roamResult )
{
   hddLog(VOS_TRACE_LEVEL_INFO, "%s: %s: id %d, status %d, result %d",
          __func__, pAdapter->dev->name, roamId, roamStatus, roamResult);

   switch( roamResult )
   {
      // both IBSS Started and IBSS Join should come in here.
      case eCSR_ROAM_RESULT_IBSS_STARTED:
      case eCSR_ROAM_RESULT_IBSS_JOIN_SUCCESS:
      case eCSR_ROAM_RESULT_IBSS_COALESCED:
      {
         hdd_context_t *pHddCtx = (hdd_context_t*)pAdapter->pHddCtx;
         v_MACADDR_t broadcastMacAddr = VOS_MAC_ADDR_BROADCAST_INITIALIZER;

         if (NULL == pRoamInfo)
         {
            VOS_ASSERT(0);
            return;
         }

         /* When IBSS Started comes from CSR, we need to move
          * connection state to IBSS Disconnected (meaning no peers
          * are in the IBSS).
          */
         hdd_connSetConnectionState(pAdapter,
                                     eConnectionState_IbssDisconnected);
         /* Notify wmm */
         hdd_wmm_connect(pAdapter, pRoamInfo, eCSR_BSS_TYPE_IBSS);
         pHddCtx->sta_to_adapter[IBSS_BROADCAST_STAID] = pAdapter;
         hdd_roamRegisterSTA (pAdapter, pRoamInfo,
                      IBSS_BROADCAST_STAID,
                      &broadcastMacAddr, pRoamInfo->pBssDesc);

         if (pRoamInfo->pBssDesc)
         {
            struct cfg80211_bss *bss;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)) || defined(WITH_BACKPORTS)
            struct ieee80211_channel *chan;
            int chan_no;
            unsigned int freq;
#endif
            /* we created the IBSS, notify supplicant */
            hddLog(VOS_TRACE_LEVEL_INFO, "%s: %s: created ibss "
                   MAC_ADDRESS_STR,
                   __func__, pAdapter->dev->name,
                   MAC_ADDR_ARRAY(pRoamInfo->pBssDesc->bssId));

            /* we must first give cfg80211 the BSS information */
            bss = wlan_hdd_cfg80211_update_bss_db(pAdapter, pRoamInfo);
            if (NULL == bss)
            {
               hddLog(VOS_TRACE_LEVEL_ERROR,
                      "%s: %s: unable to create IBSS entry",
                      __func__, pAdapter->dev->name);
               return;
            }
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,15,0)) || defined(WITH_BACKPORTS)
            chan_no = pRoamInfo->pBssDesc->channelId;

            if (chan_no <= 14)
                freq = ieee80211_channel_to_frequency(chan_no,
                                                      HDD_NL80211_BAND_2GHZ);
            else
                freq = ieee80211_channel_to_frequency(chan_no,
                                                      HDD_NL80211_BAND_5GHZ);

            chan = ieee80211_get_channel(pAdapter->wdev.wiphy, freq);

            if (chan)
                cfg80211_ibss_joined(pAdapter->dev, bss->bssid,
                                     chan, GFP_KERNEL);
            else
                hddLog(LOGE, FL("%s: chanId: %d, can't find channel"),
                       pAdapter->dev->name,
                       (int)pRoamInfo->pBssDesc->channelId);
#else

            wlan_hdd_netif_queue_control(pAdapter,
                WLAN_START_ALL_NETIF_QUEUE_N_CARRIER,
                WLAN_CONTROL_PATH);
            cfg80211_ibss_joined(pAdapter->dev, bss->bssid, GFP_KERNEL);
#endif
            cfg80211_put_bss(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)) || defined(WITH_BACKPORTS)
                             pHddCtx->wiphy,
#endif
                             bss);
         }

         break;
      }

      case eCSR_ROAM_RESULT_IBSS_START_FAILED:
      {
         hddLog(VOS_TRACE_LEVEL_ERROR, "%s: %s: unable to create IBSS",
                __func__, pAdapter->dev->name);
         break;
      }

      default:
         hddLog(VOS_TRACE_LEVEL_ERROR, "%s: %s: unexpected result %d",
                __func__, pAdapter->dev->name, (int)roamResult);
         break;
   }

   return;
}

/**
 * hdd_save_peer() - Save peer MAC address in adapter peer table.
 * @sta_ctx: pointer to hdd station context
 * @sta_id: station ID
 * @peer_mac_addr: mac address of new peer
 *
 * This information is passed to iwconfig later. The peer that joined
 * last is passed as information to iwconfig.

 * Return: true if success, false otherwise
 */
bool hdd_save_peer(hdd_station_ctx_t *sta_ctx, uint8_t sta_id,
		   v_MACADDR_t *peer_mac_addr)
{
	int idx;

	for (idx = 0; idx < HDD_MAX_NUM_IBSS_STA; idx++) {
		if (0 == sta_ctx->conn_info.staId[idx]) {
			hddLog(VOS_TRACE_LEVEL_DEBUG,
			       FL("adding peer: %pM, sta_id: %d, at idx: %d"),
			       peer_mac_addr, sta_id, idx);
			sta_ctx->conn_info.staId[idx] = sta_id;
			vos_copy_macaddr(
				&sta_ctx->conn_info.peerMacAddress[idx],
				peer_mac_addr);
			return true;
		}
	}
	return false;
}

/**
 * hdd_delete_peer() - removes peer from hdd station context peer table
 * @sta_ctx: pointer to hdd station context
 * @sta_id: station ID
 *
 * Return: none
 */
void hdd_delete_peer(hdd_station_ctx_t *sta_ctx, uint8_t sta_id)
{
	int i;

	for (i = 0; i < HDD_MAX_NUM_IBSS_STA; i++) {
		if (sta_id == sta_ctx->conn_info.staId[i]) {
			sta_ctx->conn_info.staId[i] = 0;
			return;
		}
	}

	hddLog(LOGE, FL("sta_id %d is not present in peer table"), sta_id);
}

/**============================================================================
 *
  @brief roamRemoveIbssStation() - Remove the IBSS peer MAC address in the adapter.
  If we remove HDD_MAX_NUM_IBSS_STA or less STA we return success else we
  return FALSE.

  ===========================================================================*/
static int roamRemoveIbssStation( hdd_adapter_t *pAdapter, v_U8_t staId )
{
   int fSuccess = FALSE;
   int idx = 0;
   v_U8_t  valid_idx   = 0;
   v_U8_t  del_idx   = 0;
   v_U8_t  empty_slots = 0;
   hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);

   for ( idx = 0; idx < HDD_MAX_NUM_IBSS_STA; idx++ )
   {
      if ( staId == pHddStaCtx->conn_info.staId[ idx ] )
      {
         pHddStaCtx->conn_info.staId[ idx ] = 0;

         vos_zero_macaddr( &pHddStaCtx->conn_info.peerMacAddress[ idx ] );

         fSuccess = TRUE;

         // Note the deleted Index, if its 0 we need special handling
         del_idx = idx;

         empty_slots++;
      }
      else
      {
         if (pHddStaCtx->conn_info.staId[idx] != 0)
         {
            valid_idx = idx;
         }
         else
         {
            // Found an empty slot
            empty_slots++;
         }
      }
   }

   if (HDD_MAX_NUM_IBSS_STA == empty_slots)
   {
      // Last peer departed, set the IBSS state appropriately
      pHddStaCtx->conn_info.connState = eConnectionState_IbssDisconnected;
      VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                 "Last IBSS Peer Departed!!!" );
   }

   // Find next active staId, to have a valid sta trigger for TL.
   if (fSuccess == TRUE)
   {
      if (del_idx == 0)
      {
         if (pHddStaCtx->conn_info.staId[valid_idx] != 0)
         {
            pHddStaCtx->conn_info.staId[0] = pHddStaCtx->conn_info.staId[valid_idx];
            vos_copy_macaddr( &pHddStaCtx->conn_info.peerMacAddress[ 0 ],
               &pHddStaCtx->conn_info.peerMacAddress[ valid_idx ]);

            pHddStaCtx->conn_info.staId[valid_idx] = 0;
            vos_zero_macaddr( &pHddStaCtx->conn_info.peerMacAddress[ valid_idx ] );
         }
      }
   }
   return( fSuccess );
}

/**============================================================================
 *
  @brief roamIbssConnectHandler() : We update the status of the IBSS to
  connected in this function.

  ===========================================================================*/
static eHalStatus roamIbssConnectHandler( hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo )
{
   struct cfg80211_bss *bss;
   hddLog(LOG1, FL("IBSS Connect Indication from SME!!! "));
   // Set the internal connection state to show 'IBSS Connected' (IBSS with a partner stations)...
   hdd_connSetConnectionState(pAdapter,
                              eConnectionState_IbssConnected);

   // Save the connection info from CSR...
   hdd_connSaveConnectInfo( pAdapter, pRoamInfo, eCSR_BSS_TYPE_IBSS );

   // Send the bssid address to the wext.
   hdd_SendAssociationEvent(pAdapter->dev, pRoamInfo);
   /* add bss_id to cfg80211 data base */
   bss = wlan_hdd_cfg80211_update_bss_db(pAdapter, pRoamInfo);
   if (NULL == bss)
   {
      hddLog(VOS_TRACE_LEVEL_ERROR,
             "%s: %s: unable to create IBSS entry",
             __func__, pAdapter->dev->name);
      return eHAL_STATUS_FAILURE;
   }
   cfg80211_put_bss(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)) || defined(WITH_BACKPORTS)
                    WLAN_HDD_GET_CTX(pAdapter)->wiphy,
#endif
                    bss);

   return( eHAL_STATUS_SUCCESS );
}
/**============================================================================
 *
  @brief hdd_RoamSetKeyCompleteHandler() - Update the security parameters.

  ===========================================================================*/
static eHalStatus hdd_RoamSetKeyCompleteHandler( hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo,
                                                 tANI_U32 roamId, eRoamCmdStatus roamStatus,
                                                 eCsrRoamResult roamResult )
{
   eCsrEncryptionType connectedCipherAlgo;
   v_BOOL_t fConnected   = FALSE;
   VOS_STATUS vosStatus    = VOS_STATUS_E_FAILURE;
   hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
   hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
   ENTER();

   if (NULL == pRoamInfo)
   {
       VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH, "pRoamInfo is NULL");
       return eHAL_STATUS_FAILURE;
   }
   // if ( WPA ), tell TL to go to 'authenticated' after the keys are set.
   // then go to 'authenticated'.  For all other authentication types (those that do
   // not require upper layer authentication) we can put TL directly into 'authenticated'
   // state.
   VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
       "Set Key completion roamStatus=%d roamResult=%d encryptionType=%d "
       MAC_ADDRESS_STR, roamStatus, roamResult,
       pHddStaCtx->conn_info.ucEncryptionType,
       MAC_ADDR_ARRAY(pRoamInfo->peerMac));

   fConnected = hdd_connGetConnectedCipherAlgo( pHddStaCtx, &connectedCipherAlgo );
   if( fConnected )
   {
      if ( WLAN_HDD_IBSS == pAdapter->device_mode )
      {
         v_U8_t staId;

         v_MACADDR_t broadcastMacAddr = VOS_MAC_ADDR_BROADCAST_INITIALIZER;

         if ( 0 == memcmp( pRoamInfo->peerMac,
                      &broadcastMacAddr, VOS_MAC_ADDR_SIZE ) )
         {
            vosStatus = WLANTL_STAPtkInstalled( pHddCtx->pvosContext,
                                                IBSS_BROADCAST_STAID);
            pHddStaCtx->roam_info.roamingState = HDD_ROAM_STATE_NONE;
         }
         else
         {
            vosStatus = hdd_get_peer_sta_id(pHddStaCtx,
                              (v_MACADDR_t*)pRoamInfo->peerMac,
                              &staId);
            if ( VOS_STATUS_SUCCESS == vosStatus )
            {
               VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
                "WLAN TL STA Ptk Installed for STAID=%d", staId);
               vosStatus = WLANTL_STAPtkInstalled( pHddCtx->pvosContext,
                                                  staId);
               pHddStaCtx->roam_info.roamingState = HDD_ROAM_STATE_NONE;
            }
         }
      }
      else
      {
         /*
          * TODO: Considering getting a state machine in HDD later.
          * This routine is invoked twice. 1)set PTK 2)set GTK.
          * The following if statement will be TRUE when setting GTK.
          * At this time we don't handle the state in detail.
          * Related CR: 174048 - TL not in authenticated state
          */
      if (eCSR_ROAM_RESULT_AUTHENTICATED == roamResult) {
          pHddStaCtx->conn_info.gtk_installed = true;
          /*
           * PTK exchange happens in preauthentication itself if key_mgmt is
           * FT-PSK, ptk_installed was false as there is no set PTK after
           * roaming. STA TL state moves to athenticated only if ptk_installed
           * is true. So, make ptk_installed to true in case of 11R roaming.
           */
#ifdef WLAN_FEATURE_VOWIFI_11R
          if (pRoamInfo->is11rAssoc)
              pHddStaCtx->conn_info.ptk_installed = true;
#endif
      } else {
          pHddStaCtx->conn_info.ptk_installed = true;
      }

         /* In WPA case move STA to authenticated when ptk is installed.
          * Earlier in WEP case STA was moved to AUTHENTICATED prior to
          * setting the unicast key and it was resulting in sending
          * few un-encrypted packet. Now in WEP case STA state will
          * be moved to AUTHENTICATED after we set the unicast
          * and broadcast key.
          */
      if ((pHddStaCtx->conn_info.ucEncryptionType ==
          eCSR_ENCRYPT_TYPE_WEP40) ||
          (pHddStaCtx->conn_info.ucEncryptionType ==
          eCSR_ENCRYPT_TYPE_WEP104) ||
          (pHddStaCtx->conn_info.ucEncryptionType ==
          eCSR_ENCRYPT_TYPE_WEP40_STATICKEY) ||
          (pHddStaCtx->conn_info.ucEncryptionType ==
          eCSR_ENCRYPT_TYPE_WEP104_STATICKEY)) {
          if (pHddStaCtx->conn_info.gtk_installed &&
              pHddStaCtx->conn_info.ptk_installed)
              vosStatus = hdd_change_sta_state_authenticated(pAdapter,
                                    pRoamInfo);
      } else if (pHddStaCtx->conn_info.ptk_installed)
          vosStatus = hdd_change_sta_state_authenticated(pAdapter,
                                pRoamInfo);

      if (pHddStaCtx->conn_info.gtk_installed &&
          pHddStaCtx->conn_info.ptk_installed) {
          pHddStaCtx->conn_info.gtk_installed = false;
          pHddStaCtx->conn_info.ptk_installed = false;
      }

         pHddStaCtx->roam_info.roamingState = HDD_ROAM_STATE_NONE;
      }
   }
   else
   {
      // possible disassoc after issuing set key and waiting set key complete
      pHddStaCtx->roam_info.roamingState = HDD_ROAM_STATE_NONE;
   }

   EXIT();
   return( eHAL_STATUS_SUCCESS );
}
/**============================================================================
 *
  @brief hdd_RoamMicErrorIndicationHandler() - This function indicates the Mic failure to the supplicant.
  ===========================================================================*/
static eHalStatus hdd_RoamMicErrorIndicationHandler( hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo,
                                                 tANI_U32 roamId, eRoamCmdStatus roamStatus,                                                                              eCsrRoamResult roamResult )
{
   hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);

   if( eConnectionState_Associated == pHddStaCtx->conn_info.connState &&
      TKIP_COUNTER_MEASURE_STOPED == pHddStaCtx->WextState.mTKIPCounterMeasures )
   {
      struct iw_michaelmicfailure msg;
      union iwreq_data wreq;
      memset(&msg, '\0', sizeof(msg));
      msg.src_addr.sa_family = ARPHRD_ETHER;
      memcpy(msg.src_addr.sa_data, pRoamInfo->u.pMICFailureInfo->taMacAddr, sizeof(pRoamInfo->u.pMICFailureInfo->taMacAddr));
      hddLog(LOG1, "MIC MAC " MAC_ADDRESS_STR,
             MAC_ADDR_ARRAY(msg.src_addr.sa_data));

      if(pRoamInfo->u.pMICFailureInfo->multicast == eSIR_TRUE)
         msg.flags = IW_MICFAILURE_GROUP;
      else
         msg.flags = IW_MICFAILURE_PAIRWISE;
      memset(&wreq, 0, sizeof(wreq));
      wreq.data.length = sizeof(msg);
      wireless_send_event(pAdapter->dev, IWEVMICHAELMICFAILURE, &wreq, (char *)&msg);
      /* inform mic failure to nl80211 */
      cfg80211_michael_mic_failure(pAdapter->dev,
              pRoamInfo->u.pMICFailureInfo->taMacAddr,
              ((pRoamInfo->u.pMICFailureInfo->multicast == eSIR_TRUE) ?
               NL80211_KEYTYPE_GROUP :
               NL80211_KEYTYPE_PAIRWISE),
              pRoamInfo->u.pMICFailureInfo->keyId,
              pRoamInfo->u.pMICFailureInfo->TSC,
              GFP_KERNEL);

   }

   return( eHAL_STATUS_SUCCESS );
}

/**============================================================================
 *
  @brief roamRoamConnectStatusUpdateHandler() - The Ibss connection status is
  updated regularly here in this function.

  ===========================================================================*/
static eHalStatus roamRoamConnectStatusUpdateHandler( hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo,
   tANI_U32 roamId, eRoamCmdStatus roamStatus,
   eCsrRoamResult roamResult )
{
   VOS_STATUS vosStatus;

   hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
   switch( roamResult )
   {
      case eCSR_ROAM_RESULT_IBSS_NEW_PEER:
      {
         hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
         struct station_info *stainfo;

         VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                   "IBSS New Peer indication from SME with peerMac " MAC_ADDRESS_STR " BSSID: " MAC_ADDRESS_STR " and stationID= %d",
                    MAC_ADDR_ARRAY(pRoamInfo->peerMac),
                    MAC_ADDR_ARRAY(pHddStaCtx->conn_info.bssId),
                    pRoamInfo->staId );

         if (!hdd_save_peer(WLAN_HDD_GET_STATION_CTX_PTR(pAdapter),
                            pRoamInfo->staId,
                            (v_MACADDR_t *)pRoamInfo->peerMac)) {
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_WARN,
                       "New IBSS peer but we already have the max we can handle.  Can't register this one" );
            break;
         }

         pHddCtx->sta_to_adapter[pRoamInfo->staId] = pAdapter;

         pHddCtx->sta_to_adapter[IBSS_BROADCAST_STAID] = pAdapter;
         WLANTL_UpdateSTABssIdforIBSS(pHddCtx->pvosContext,
                      IBSS_BROADCAST_STAID,pHddStaCtx->conn_info.bssId);

         // Register the Station with TL for the new peer.
         vosStatus = hdd_roamRegisterSTA( pAdapter,
                                          pRoamInfo,
                                          pRoamInfo->staId,
                                          (v_MACADDR_t *)pRoamInfo->peerMac,
                                          pRoamInfo->pBssDesc );
         if ( !VOS_IS_STATUS_SUCCESS( vosStatus ) )
         {
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
               "Cannot register STA with TL for IBSS.  Failed with vosStatus = %d [%08X]",
               vosStatus, vosStatus );
         }
         pHddStaCtx->ibss_sta_generation++;
         stainfo = vos_mem_malloc(sizeof(*stainfo));
         if (stainfo == NULL) {
             VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                       "memory allocation for station_info failed");
             return eHAL_STATUS_FAILED_ALLOC;
         }
         memset(stainfo, 0, sizeof(*stainfo));
         stainfo->filled = 0;
         stainfo->generation = pHddStaCtx->ibss_sta_generation;

         cfg80211_new_sta(pAdapter->dev,
                          (const u8 *)pRoamInfo->peerMac,
                          stainfo, GFP_KERNEL);
         vos_mem_free(stainfo);

         if ( eCSR_ENCRYPT_TYPE_WEP40_STATICKEY == pHddStaCtx->ibss_enc_key.encType
            ||eCSR_ENCRYPT_TYPE_WEP104_STATICKEY == pHddStaCtx->ibss_enc_key.encType
            ||eCSR_ENCRYPT_TYPE_TKIP == pHddStaCtx->ibss_enc_key.encType
            ||eCSR_ENCRYPT_TYPE_AES == pHddStaCtx->ibss_enc_key.encType )
         {
            pHddStaCtx->ibss_enc_key.keyDirection = eSIR_TX_RX;
            memcpy(&pHddStaCtx->ibss_enc_key.peerMac,
                              pRoamInfo->peerMac, VOS_MAC_ADDR_SIZE);

            VOS_TRACE( VOS_MODULE_ID_HDD,
               VOS_TRACE_LEVEL_INFO_HIGH, "New peer joined set PTK encType=%d",
               pHddStaCtx->ibss_enc_key.encType);

            vosStatus = sme_RoamSetKey( WLAN_HDD_GET_HAL_CTX(pAdapter),
               pAdapter->sessionId, &pHddStaCtx->ibss_enc_key, &roamId );

            if ( VOS_STATUS_SUCCESS != vosStatus )
            {
               hddLog(VOS_TRACE_LEVEL_ERROR,
                       "%s: sme_RoamSetKey failed, returned %d",
                       __func__, vosStatus);
               return eHAL_STATUS_FAILURE;
            }
         }
         hddLog(LOG1, FL("Enabling queues"));
         wlan_hdd_netif_queue_control(pAdapter,
            WLAN_START_ALL_NETIF_QUEUE_N_CARRIER,
            WLAN_CONTROL_PATH);
         break;
      }

      case eCSR_ROAM_RESULT_IBSS_CONNECT:
      {

         roamIbssConnectHandler( pAdapter, pRoamInfo );

         break;
      }
      case eCSR_ROAM_RESULT_IBSS_PEER_DEPARTED:
      {
         hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);

         if (!roamRemoveIbssStation(pAdapter, pRoamInfo->staId)) {
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_WARN,
                    "IBSS peer departed by cannot find peer in our registration table with TL" );
         }

          VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                   "IBSS Peer Departed from SME with peerMac " MAC_ADDRESS_STR " BSSID: " MAC_ADDRESS_STR " and stationID= %d",
                    MAC_ADDR_ARRAY(pRoamInfo->peerMac),
                    MAC_ADDR_ARRAY(pHddStaCtx->conn_info.bssId),
                    pRoamInfo->staId );

         hdd_roamDeregisterSTA( pAdapter, pRoamInfo->staId );

         pHddCtx->sta_to_adapter[pRoamInfo->staId] = NULL;
         pHddStaCtx->ibss_sta_generation++;

         cfg80211_del_sta(pAdapter->dev,
                         (const u8 *)&pRoamInfo->peerMac,
                         GFP_KERNEL);
         break;
      }
      case eCSR_ROAM_RESULT_IBSS_INACTIVE:
      {
          VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_MED,
                    "Received eCSR_ROAM_RESULT_IBSS_INACTIVE from SME");
         // Stop only when we are inactive
         hddLog(LOG1, FL("Disabling queues"));
         wlan_hdd_netif_queue_control(pAdapter,
            WLAN_NETIF_TX_DISABLE_N_CARRIER,
            WLAN_CONTROL_PATH);

         hdd_connSetConnectionState(pAdapter,
                                     eConnectionState_NotConnected);

         // Send the bssid address to the wext.
         hdd_SendAssociationEvent(pAdapter->dev, pRoamInfo);
         // clean up data path
         hdd_disconnect_tx_rx(pAdapter);
         break;
      }
      default:
         break;

   }

   return( eHAL_STATUS_SUCCESS );
}

#ifdef FEATURE_WLAN_TDLS
/**============================================================================
 *
  @brief hdd_roamRegisterTDLSSTA() - Construct the staDesc and register with
  TL the new STA. This is called as part of ADD_STA in the TDLS setup
  Return: VOS_STATUS

  ===========================================================================*/
VOS_STATUS hdd_roamRegisterTDLSSTA(hdd_adapter_t *pAdapter,
                                   const tANI_U8 *peerMac, tANI_U16 staId,
                                   tANI_U8 ucastSig, uint8_t qos)
{
    hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
    v_CONTEXT_t pVosContext = (WLAN_HDD_GET_CTX(pAdapter))->pvosContext;
    VOS_STATUS vosStatus = VOS_STATUS_E_FAILURE;
    WLAN_STADescType staDesc = {0};
    eCsrEncryptionType connectedCipherAlgo = eCSR_ENCRYPT_TYPE_UNKNOWN;
    v_BOOL_t fConnected   = FALSE;

    fConnected = hdd_connGetConnectedCipherAlgo( pHddStaCtx, &connectedCipherAlgo );
    if (!fConnected) {
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                     "%s not connected. ignored", __func__);
        return VOS_FALSE;
    }

    /*
     * TDLS sta in BSS should be set as STA type TDLS and STA MAC should
     * be peer MAC, here we are working on direct Link
     */
    staDesc.ucSTAId = staId ;

    staDesc.wSTAType = WLAN_STA_TDLS ;

    vos_mem_copy( staDesc.vSTAMACAddress.bytes, peerMac,
                                         sizeof(tSirMacAddr) );

    vos_mem_copy(staDesc.vBSSIDforIBSS.bytes, pHddStaCtx->conn_info.bssId,6 );
    vos_copy_macaddr( &staDesc.vSelfMACAddress, &pAdapter->macAddressCurrent );

    /* set the QoS field appropriately ..*/
    staDesc.ucQosEnabled = qos;

    VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
               "HDD register TL QoS_enabled=%d", staDesc.ucQosEnabled );

    staDesc.ucProtectedFrame = (connectedCipherAlgo != eCSR_ENCRYPT_TYPE_NONE) ;

    VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_MED,
               "HDD register TL Sec_enabled= %d.", staDesc.ucProtectedFrame );

    /*
     * UMA is ready we inform TL  to do frame translation.
     */
    staDesc.ucSwFrameTXXlation = 1;
    staDesc.ucSwFrameRXXlation = 1;
    staDesc.ucAddRmvLLC = 1;

    /* Initialize signatures and state */
    staDesc.ucUcastSig  = ucastSig ;

    /* tdls Direct Link do not need bcastSig */
    staDesc.ucBcastSig  = 0 ;

#ifdef VOLANS_ENABLE_SW_REPLAY_CHECK
    if(staDesc.ucProtectedFrame)
        staDesc.ucIsReplayCheckValid = VOS_TRUE;
    else
        staDesc.ucIsReplayCheckValid = VOS_FALSE;
#endif

    staDesc.ucInitState = WLANTL_STA_CONNECTED ;

   /* Register the Station with TL...  */
   /* Incase Micro controller data path offload enabled,
    * All the traffic routed to WLAN host driver, do not need to
    * route IPA. It should be routed kernel network stack */
#if defined(IPA_OFFLOAD) && !defined(IPA_UC_OFFLOAD)
    if (hdd_ipa_is_enabled(WLAN_HDD_GET_CTX(pAdapter)))
       vosStatus = WLANTL_RegisterSTAClient(pVosContext,
                                          hdd_ipa_process_rxt,
                                          &staDesc, 0);
    else
#endif
    vosStatus = WLANTL_RegisterSTAClient(pVosContext,
                                          hdd_rx_packet_cbk,
                                          &staDesc, 0);

    if ( !VOS_IS_STATUS_SUCCESS( vosStatus ) )
    {
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                   "%s: WLANTL_RegisterSTAClient() failed to register.  "
                   "Status= %d [0x%08X]", __func__, vosStatus, vosStatus );
         return vosStatus;
    }

    return( vosStatus );
}

VOS_STATUS hdd_roamDeregisterTDLSSTA(hdd_adapter_t *pAdapter, tANI_U8 staId)
{
    VOS_STATUS vosStatus;
    vosStatus = WLANTL_ClearSTAClient( (WLAN_HDD_GET_CTX(pAdapter))->pvosContext, staId );
    if ( !VOS_IS_STATUS_SUCCESS( vosStatus ) )
    {
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_WARN,
                   "%s: WLANTL_ClearSTAClient() failed to for staID %d.  "
                   "Status= %d [0x%08X]",
                   __func__, staId, vosStatus, vosStatus );
    }
    return( vosStatus );
}


/*
 * HDD interface between SME and TL to ensure TDLS client registration with
 * TL in case of new TDLS client is added and deregistration at the time
 * TDLS client is deleted.
 */

static eHalStatus
hdd_RoamTdlsStatusUpdateHandler(hdd_adapter_t *pAdapter,
                                tCsrRoamInfo *pRoamInfo, tANI_U32 roamId,
                                eRoamCmdStatus roamStatus,
                                eCsrRoamResult roamResult)
{
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
#ifdef CONFIG_TDLS_IMPLICIT
    tdlsCtx_t *pHddTdlsCtx = WLAN_HDD_GET_TDLS_CTX_PTR(pAdapter);
#endif
    tSmeTdlsPeerStateParams smeTdlsPeerStateParams;
    eHalStatus status = eHAL_STATUS_FAILURE ;
    tANI_U8 staIdx;
    hddTdlsPeer_t *curr_peer;
    tANI_U32 reason;

    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
    ("hdd_tdlsStatusUpdate: %s staIdx %d " MAC_ADDRESS_STR),
      roamResult == eCSR_ROAM_RESULT_ADD_TDLS_PEER ? "ADD_TDLS_PEER" :
      roamResult == eCSR_ROAM_RESULT_DELETE_TDLS_PEER ? "DEL_TDLS_PEER" :
      roamResult == eCSR_ROAM_RESULT_TEARDOWN_TDLS_PEER_IND ? "DEL_TDLS_PEER_IND" :
      roamResult == eCSR_ROAM_RESULT_DELETE_ALL_TDLS_PEER_IND? "DEL_ALL_TDLS_PEER_IND" :
      roamResult == eCSR_ROAM_RESULT_UPDATE_TDLS_PEER? "UPDATE_TDLS_PEER" :
      roamResult == eCSR_ROAM_RESULT_LINK_ESTABLISH_REQ_RSP? "LINK_ESTABLISH_REQ_RSP" :
      roamResult == eCSR_ROAM_RESULT_TDLS_SHOULD_DISCOVER? "TDLS_SHOULD_DISCOVER" :
      roamResult == eCSR_ROAM_RESULT_TDLS_SHOULD_TEARDOWN? "TDLS_SHOULD_TEARDOWN" :
      roamResult == eCSR_ROAM_RESULT_TDLS_SHOULD_PEER_DISCONNECTED? "TDLS_SHOULD_PEER_DISCONNECTED" :
      "UNKNOWN",
       pRoamInfo->staId,
       MAC_ADDR_ARRAY(pRoamInfo->peerMac)) ;

#ifdef CONFIG_TDLS_IMPLICIT
    if (!pHddTdlsCtx)
    {
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                  "%s: TDLS ctx is null, ignore roamResult (%d)",
                  __func__, roamResult);
        return status;
    }
#endif

    switch( roamResult )
    {
        case eCSR_ROAM_RESULT_ADD_TDLS_PEER:
        {
            if(eSIR_SME_SUCCESS != pRoamInfo->statusCode)
            {
                hddTdlsPeer_t *curr_peer;
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                     ("%s: Add Sta is failed. %d"),__func__, pRoamInfo->statusCode);
                mutex_lock(&pHddCtx->tdls_lock);
                curr_peer = wlan_hdd_tdls_find_peer(pAdapter,
                                     pRoamInfo->peerMac, FALSE);
                if (curr_peer)
                    curr_peer->link_status = eTDLS_LINK_TEARING;
                else
                    hddLog(LOG1, FL("curr_peer is Null"));
                mutex_unlock(&pHddCtx->tdls_lock);
            }
            else
            {

                /* check if there is available index for this new TDLS STA */
                for ( staIdx = 0; staIdx < pHddCtx->max_num_tdls_sta; staIdx++ )
                {
                    if (0 == pHddCtx->tdlsConnInfo[staIdx].staId )
                    {
                        pHddCtx->tdlsConnInfo[staIdx].sessionId = pRoamInfo->sessionId;
                        pHddCtx->tdlsConnInfo[staIdx].staId = pRoamInfo->staId;

                        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_WARN,
                                  ("TDLS: STA IDX at %d is %d "
                                  "of mac " MAC_ADDRESS_STR),
                                  staIdx, pHddCtx->tdlsConnInfo[staIdx].staId,
                                  MAC_ADDR_ARRAY(pRoamInfo->peerMac));

                        vos_copy_macaddr(&pHddCtx->tdlsConnInfo[staIdx].peerMac,
                                         (v_MACADDR_t *)pRoamInfo->peerMac) ;
                        status = eHAL_STATUS_SUCCESS ;
                        break ;
                    }
                }
                if (staIdx < pHddCtx->max_num_tdls_sta)
                {
                    if (-1 == wlan_hdd_tdls_set_sta_id(pAdapter, pRoamInfo->peerMac, pRoamInfo->staId)) {
                        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                                     "wlan_hdd_tdls_set_sta_id() failed");
                        return VOS_FALSE;
                    }

                    (WLAN_HDD_GET_CTX(pAdapter))->sta_to_adapter[pRoamInfo->staId] = pAdapter;
                    /* store the ucast signature , if required for further reference. */

                    wlan_hdd_tdls_set_signature( pAdapter, pRoamInfo->peerMac, pRoamInfo->ucastSig );
                }
                else
                {
                    status = eHAL_STATUS_FAILURE;
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                    "%s: no available slot in conn_info. staId %d cannot be stored", __func__, pRoamInfo->staId);
                }
                pAdapter->tdlsAddStaStatus = status;
            }
            complete(&pAdapter->tdls_add_station_comp);
            break ;
        }
        case eCSR_ROAM_RESULT_UPDATE_TDLS_PEER:
        {
            if (eSIR_SME_SUCCESS != pRoamInfo->statusCode)
            {
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                     "%s: Add Sta is failed. %d", __func__, pRoamInfo->statusCode);
            }
            /* store the ucast signature which will be used later when
             * registering to TL
             */
            pAdapter->tdlsAddStaStatus = pRoamInfo->statusCode;
            complete(&pAdapter->tdls_add_station_comp);
            break;
        }
        case eCSR_ROAM_RESULT_LINK_ESTABLISH_REQ_RSP:
        {

            hddTdlsPeer_t *curr_peer;
            if (eSIR_SME_SUCCESS != pRoamInfo->statusCode)
            {
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                     "%s: Link Establish Request failed. %d", __func__, pRoamInfo->statusCode);
                mutex_lock(&pHddCtx->tdls_lock);
                curr_peer = wlan_hdd_tdls_find_peer(pAdapter,
                                          pRoamInfo->peerMac, FALSE);
                if (curr_peer)
                    curr_peer->link_status = eTDLS_LINK_TEARING;
                else
                    hddLog(LOGE, FL("curr_peer is Null"));

                mutex_unlock(&pHddCtx->tdls_lock);
            }
            complete(&pAdapter->tdls_link_establish_req_comp);
            break;
        }
        case eCSR_ROAM_RESULT_DELETE_TDLS_PEER:
        {
            for (staIdx = 0; staIdx < pHddCtx->max_num_tdls_sta; staIdx++)
            {
                if ((pHddCtx->tdlsConnInfo[staIdx].sessionId == pRoamInfo->sessionId) &&
                    pRoamInfo->staId == pHddCtx->tdlsConnInfo[staIdx].staId)
                {
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_WARN,
                                   ("HDD: del STA IDX = %x"), pRoamInfo->staId) ;

                    curr_peer = wlan_hdd_tdls_find_peer(pAdapter, pRoamInfo->peerMac, TRUE);
                    if (NULL != curr_peer)
                    {
                       VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                                 " Current status for peer" MAC_ADDRESS_STR "is %d",
                                 MAC_ADDR_ARRAY(pRoamInfo->peerMac), curr_peer->link_status);
                       if (TDLS_IS_CONNECTED(curr_peer))
                       {
                           hdd_roamDeregisterTDLSSTA ( pAdapter, pRoamInfo->staId );
                           wlan_hdd_tdls_decrement_peer_count(pAdapter);
                       }
                       else if (eTDLS_LINK_CONNECTING == curr_peer->link_status)
                       {
                           hdd_roamDeregisterTDLSSTA ( pAdapter, pRoamInfo->staId );
                       }
                    }
                    wlan_hdd_tdls_reset_peer(pAdapter, pRoamInfo->peerMac);

                    pHddCtx->tdlsConnInfo[staIdx].staId = 0 ;
                    pHddCtx->tdlsConnInfo[staIdx].sessionId = 255;
                    vos_mem_zero(&pHddCtx->tdlsConnInfo[staIdx].peerMac,
                                               sizeof(v_MACADDR_t)) ;
                    status = eHAL_STATUS_SUCCESS ;
                    break ;
                }
            }
            complete(&pAdapter->tdls_del_station_comp);
        }
        break ;
        case  eCSR_ROAM_TDLS_CHECK_BMPS:
        {
            wlan_hdd_tdls_check_bmps(pAdapter);
            status = eHAL_STATUS_SUCCESS ;
            break;
        }
        case eCSR_ROAM_RESULT_TEARDOWN_TDLS_PEER_IND:
        {
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                       "%s: Sending teardown to supplicant with reason code %u",
                       __func__, pRoamInfo->reasonCode);

#ifdef CONFIG_TDLS_IMPLICIT
            curr_peer = wlan_hdd_tdls_find_peer(pAdapter, pRoamInfo->peerMac, TRUE);
            wlan_hdd_tdls_indicate_teardown(pAdapter, curr_peer, pRoamInfo->reasonCode);
            hdd_send_wlan_tdls_teardown_event(eTDLS_TEARDOWN_BSS_DISCONNECT,
                                                          curr_peer->peerMac);
#endif
            status = eHAL_STATUS_SUCCESS ;
            break ;
        }
        case eCSR_ROAM_RESULT_DELETE_ALL_TDLS_PEER_IND:
        {
            /* 0 staIdx is assigned to AP we dont want to touch that */
            for (staIdx = 0; staIdx < pHddCtx->max_num_tdls_sta; staIdx++)
            {
                if ((pHddCtx->tdlsConnInfo[staIdx].sessionId == pRoamInfo->sessionId) &&
                    pHddCtx->tdlsConnInfo[staIdx].staId)
                {
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_WARN,
                              ("hdd_tdlsStatusUpdate: staIdx %d " MAC_ADDRESS_STR),
                                pHddCtx->tdlsConnInfo[staIdx].staId,
                                MAC_ADDR_ARRAY(pHddCtx->tdlsConnInfo[staIdx].peerMac.bytes));
                    wlan_hdd_tdls_reset_peer(pAdapter, pHddCtx->tdlsConnInfo[staIdx].peerMac.bytes);
                    hdd_roamDeregisterTDLSSTA ( pAdapter,  pHddCtx->tdlsConnInfo[staIdx].staId );
                    vos_mem_zero(&smeTdlsPeerStateParams,
                                 sizeof(smeTdlsPeerStateParams));
                    smeTdlsPeerStateParams.vdevId =
                                 pHddCtx->tdlsConnInfo[staIdx].sessionId;
                    vos_mem_copy(&smeTdlsPeerStateParams.peerMacAddr,
                                 &pHddCtx->tdlsConnInfo[staIdx].peerMac.bytes,
                                 VOS_MAC_ADDR_SIZE);
                    smeTdlsPeerStateParams.peerState =
                                 eSME_TDLS_PEER_STATE_TEARDOWN;

                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                              ("hdd_tdlsStatusUpdate: calling sme_UpdateTdlsPeerState for staIdx %d " MAC_ADDRESS_STR),
                                pHddCtx->tdlsConnInfo[staIdx].staId,
                                MAC_ADDR_ARRAY(pHddCtx->tdlsConnInfo[staIdx].peerMac.bytes));
                    status = sme_UpdateTdlsPeerState(pHddCtx->hHal,
                                                     &smeTdlsPeerStateParams);
                    if (eHAL_STATUS_SUCCESS != status)
                    {
                       VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                                 "%s: sme_UpdateTdlsPeerState failed for "
                                 MAC_ADDRESS_STR, __func__,
                                 MAC_ADDR_ARRAY(pHddCtx->tdlsConnInfo[staIdx].peerMac.bytes));
                    }
                    wlan_hdd_tdls_decrement_peer_count(pAdapter);

                    vos_mem_zero(&pHddCtx->tdlsConnInfo[staIdx].peerMac,
                                               sizeof(v_MACADDR_t)) ;
                    pHddCtx->tdlsConnInfo[staIdx].staId = 0 ;
                    pHddCtx->tdlsConnInfo[staIdx].sessionId = 255;

                    status = eHAL_STATUS_SUCCESS ;
                }
            }
            wlan_hdd_tdls_check_bmps(pAdapter);
            break ;
        }
        case eCSR_ROAM_RESULT_TDLS_SHOULD_DISCOVER:
        {
#ifdef CONFIG_TDLS_IMPLICIT
            /* ignore TDLS_SHOULD_DISCOVER if any concurrency detected */
            if (((1 << VOS_STA_MODE) != pHddCtx->concurrency_mode) ||
                (pHddCtx->no_of_active_sessions[VOS_STA_MODE] > 1)) {
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
                         FL("concurrency detected. ignore SHOULD_DISCOVER concurrency_mode: 0x%x, active_sessions: %d"),
                         pHddCtx->concurrency_mode,
                         pHddCtx->no_of_active_sessions[VOS_STA_MODE]);
                status = eHAL_STATUS_FAILURE;
                break;
            }

            if (pHddCtx->tdls_nss_switch_in_progress) {
                hddLog(LOGE,
                       FL("TDLS antenna switch is in progress, ignore SHOULD_DISCOVER"));
                status = eHAL_STATUS_SUCCESS;
                break;
            }

            curr_peer = wlan_hdd_tdls_get_peer(pAdapter, pRoamInfo->peerMac,
                                               TRUE);
            if (!curr_peer)
            {
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                          "%s: curr_peer null", __func__);
                status = eHAL_STATUS_FAILURE;
            }
            else
            {
                if (eTDLS_LINK_CONNECTED == curr_peer->link_status)
                {
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                              "%s: TDLS link status is connected, ignore SHOULD_DISCOVER", __func__);
                }
                else
                {
                    /* if external control is enabled then initiate TDLS
                     * only if forced peer is set otherwise ignore
                     * Should Discover trigger from fw
                     */
                    if (pHddCtx->cfg_ini->fTDLSExternalControl &&
                        (FALSE == curr_peer->isForcedPeer))
                    {
                        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
                                  FL("TDLS ExternalControl enabled but curr_peer is not forced, ignore SHOULD_DISCOVER"));
                        status = eHAL_STATUS_SUCCESS;
                        break;
                    }
                    else
                    {
                        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
                                  FL("initiate TDLS setup on SHOULD_DISCOVER, fTDLSExternalControl: %d, curr_peer->isForcedPeer: %d, reason: %d"),
                                  pHddCtx->cfg_ini->fTDLSExternalControl,
                                  curr_peer->isForcedPeer,
                                  pRoamInfo->reasonCode);
                    }
                    pHddTdlsCtx->curr_candidate = curr_peer;

                    wlan_hdd_tdls_implicit_send_discovery_request(pHddTdlsCtx);
                }
                status = eHAL_STATUS_SUCCESS;
            }
#else
            status = eHAL_STATUS_SUCCESS;
#endif
            break ;
        }

        case eCSR_ROAM_RESULT_TDLS_SHOULD_TEARDOWN:
        {
#ifdef CONFIG_TDLS_IMPLICIT
            curr_peer = wlan_hdd_tdls_find_peer(pAdapter, pRoamInfo->peerMac, TRUE);
            if (!curr_peer)
            {
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                          "%s: curr_peer null", __func__);
                status = eHAL_STATUS_FAILURE;
            }
            else
            {
                if (eTDLS_LINK_CONNECTED == curr_peer->link_status)
                {
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                              FL("Received SHOULD_TEARDOWN for peer "
                                  MAC_ADDRESS_STR " staId: %d, reason: %d"),
                              MAC_ADDR_ARRAY(pRoamInfo->peerMac),
                              pRoamInfo->staId,
                              pRoamInfo->reasonCode);

                    if (pRoamInfo->reasonCode ==
                            eWNI_TDLS_TEARDOWN_REASON_RSSI ||
                        pRoamInfo->reasonCode ==
                            eWNI_TDLS_DISCONNECTED_REASON_PEER_DELETE ||
                        pRoamInfo->reasonCode ==
                            eWNI_TDLS_TEARDOWN_REASON_PTR_TIMEOUT ||
                        pRoamInfo->reasonCode ==
                            eWNI_TDLS_TEARDOWN_REASON_NO_RESPONSE)
                    {
                        reason = eSIR_MAC_TDLS_TEARDOWN_PEER_UNREACHABLE;
                    }
                    else
                        reason = eSIR_MAC_TDLS_TEARDOWN_UNSPEC_REASON;

                    wlan_hdd_tdls_indicate_teardown(pHddTdlsCtx->pAdapter,
                                                    curr_peer,
                                                    reason);
                    hdd_send_wlan_tdls_teardown_event(
                                       eTDLS_TEARDOWN_BSS_DISCONNECT,
                                       curr_peer->peerMac);
                }
                else
                {
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                              FL("TDLS link is not connected, ignore SHOULD_TEARDOWN, reason: %d"),
                              pRoamInfo->reasonCode);
                }
                status = eHAL_STATUS_SUCCESS;
            }
#else
            status = eHAL_STATUS_SUCCESS;
#endif
            break ;
        }

        case eCSR_ROAM_RESULT_TDLS_SHOULD_PEER_DISCONNECTED:
        {
#ifdef CONFIG_TDLS_IMPLICIT
            curr_peer = wlan_hdd_tdls_find_peer(pAdapter, pRoamInfo->peerMac, TRUE);
            if (!curr_peer)
            {
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                          "%s: curr_peer null", __func__);
                status = eHAL_STATUS_FAILURE;
            }
            else
            {
                if (eTDLS_LINK_CONNECTED == curr_peer->link_status)
                {
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                              FL("Received SHOULD_PEER_DISCONNECTED for peer "
                                  MAC_ADDRESS_STR " staId: %d, reason: %d"),
                              MAC_ADDR_ARRAY(pRoamInfo->peerMac),
                              pRoamInfo->staId,
                              pRoamInfo->reasonCode);

                    if (pRoamInfo->reasonCode ==
                            eWNI_TDLS_TEARDOWN_REASON_RSSI ||
                        pRoamInfo->reasonCode ==
                            eWNI_TDLS_DISCONNECTED_REASON_PEER_DELETE ||
                        pRoamInfo->reasonCode ==
                            eWNI_TDLS_TEARDOWN_REASON_PTR_TIMEOUT ||
                        pRoamInfo->reasonCode ==
                            eWNI_TDLS_TEARDOWN_REASON_NO_RESPONSE)
                    {
                        reason = eSIR_MAC_TDLS_TEARDOWN_PEER_UNREACHABLE;
                    }
                    else
                        reason = eSIR_MAC_TDLS_TEARDOWN_UNSPEC_REASON;

                    wlan_hdd_tdls_indicate_teardown(pHddTdlsCtx->pAdapter,
                                                    curr_peer,
                                                    reason);
                    hdd_send_wlan_tdls_teardown_event(
                                         eTDLS_TEARDOWN_BSS_DISCONNECT,
                                         curr_peer->peerMac);
                }
                else
                {
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                              FL("TDLS link is not connected, ignore SHOULD_PEER_DISCONNECTED, reason: %d"),
                              pRoamInfo->reasonCode);
                }
                status = eHAL_STATUS_SUCCESS;
            }
#else
            status = eHAL_STATUS_SUCCESS;
#endif
            break ;
        }
        default:
        {
            break ;
        }
    }

    return status ;
}
#endif

static void iw_full_power_cbfn (void *pContext, eHalStatus status)
{
    hdd_adapter_t *pAdapter = (hdd_adapter_t *)pContext;
    hdd_context_t *pHddCtx = NULL;
    int ret;

    ENTER();

    if ((NULL == pAdapter) || (WLAN_HDD_ADAPTER_MAGIC != pAdapter->magic))
    {
        hddLog(VOS_TRACE_LEVEL_ERROR,
             "%s: Bad param, pAdapter [%pK]",
               __func__, pAdapter);
        return;
    }

    pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
    ret = wlan_hdd_validate_context(pHddCtx);
    if (0 != ret)
        return;

    if (pHddCtx->cfg_ini->fIsBmpsEnabled)
    {
        sme_RequestBmps(WLAN_HDD_GET_HAL_CTX(pAdapter), NULL, NULL);
    }
    EXIT();
}

#ifdef WLAN_FEATURE_11W
/**
 * hdd_indicateUnprotMgmtFrame() - indicate unprotected management frame
 * @pAdapter:     pointer to the adapter
 * @nFrameLength: Length of the unprotected frame being passed
 * @pbFrames:     Pointer to the frame buffer
 * @frameType:    802.11 frame type
 *
 * This function forwards the unprotected management frame to the supplicant.
 *
 * Return: nothing
 */
static void
hdd_indicateUnprotMgmtFrame(hdd_adapter_t *pAdapter, tANI_U32 nFrameLength,
                            tANI_U8* pbFrames, tANI_U8 frameType)
{
    tANI_U8 type = 0;
    tANI_U8 subType = 0;

    hddLog(LOG1, FL("Frame Type(%d) Frame Length(%d)"),
           frameType, nFrameLength);

    /* Sanity Checks */
    if (NULL == pAdapter) {
        hddLog(LOGE, FL("pAdapter is NULL"));
        return;
    }

    if (NULL == pAdapter->dev) {
        hddLog(LOGE, FL("pAdapter->dev is NULL"));
        return;
    }

    if (WLAN_HDD_ADAPTER_MAGIC != pAdapter->magic) {
        hddLog(LOGE, FL("pAdapter has invalid magic"));
        return;
    }

    if(!nFrameLength) {
        hddLog(LOGE, FL("Frame Length is Invalid ZERO"));
        return;
    }

    if (NULL == pbFrames) {
        hddLog(LOGE, FL("pbFrames is NULL"));
        return;
    }

    type = WLAN_HDD_GET_TYPE_FRM_FC(pbFrames[0]);
    subType = WLAN_HDD_GET_SUBTYPE_FRM_FC(pbFrames[0]);

    /* Get pAdapter from Destination mac address of the frame */
    if (type == SIR_MAC_MGMT_FRAME && subType == SIR_MAC_MGMT_DISASSOC) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)) || defined(WITH_BACKPORTS)
        cfg80211_rx_unprot_mlme_mgmt(pAdapter->dev, pbFrames, nFrameLength);
#else
        cfg80211_send_unprot_disassoc(pAdapter->dev, pbFrames, nFrameLength);
#endif
        pAdapter->hdd_stats.hddPmfStats.numUnprotDisassocRx++;
    } else if (type == SIR_MAC_MGMT_FRAME && subType == SIR_MAC_MGMT_DEAUTH) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)) || defined(WITH_BACKPORTS)
        cfg80211_rx_unprot_mlme_mgmt(pAdapter->dev, pbFrames, nFrameLength);
#else
        cfg80211_send_unprot_deauth(pAdapter->dev, pbFrames, nFrameLength);
#endif
        pAdapter->hdd_stats.hddPmfStats.numUnprotDeauthRx++;
    } else {
        hddLog(LOGE, FL("Frame type %d and subtype %d are not valid"), type, subType);
        return;
    }
}
#endif

#if defined (FEATURE_WLAN_ESE) && defined(FEATURE_WLAN_ESE_UPLOAD)
static void
hdd_indicateTsmIe(hdd_adapter_t *pAdapter, tANI_U8 tid,
                  tANI_U8 state, tANI_U16 measInterval)
{
    union iwreq_data wrqu;
    char buf[IW_CUSTOM_MAX + 1];
    int nBytes = 0;

    if (NULL == pAdapter)
        return;

    // create the event
    memset(&wrqu, '\0', sizeof(wrqu));
    memset(buf, '\0', sizeof(buf));

    hddLog(VOS_TRACE_LEVEL_INFO, "TSM Ind tid(%d) state(%d) MeasInt(%d)",
                        tid, state, measInterval);

    nBytes = snprintf(buf, IW_CUSTOM_MAX, "TSMIE=%d:%d:%d",tid,state,measInterval);

    wrqu.data.pointer = buf;
    wrqu.data.length = nBytes;
    // send the event
    wireless_send_event(pAdapter->dev, IWEVCUSTOM, &wrqu, buf);
}

void hdd_indicateCckmPreAuth(hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo)
{
    union iwreq_data wrqu;
    char buf[IW_CUSTOM_MAX + 1];
    char *pos = buf;
    int nBytes = 0, freeBytes = IW_CUSTOM_MAX;

    if ((NULL == pAdapter) || (NULL == pRoamInfo))
        return;

    // create the event
    memset(&wrqu, '\0', sizeof(wrqu));
    memset(buf, '\0', sizeof(buf));

    /* Timestamp0 is lower 32 bits and Timestamp1 is upper 32 bits */
    hddLog(VOS_TRACE_LEVEL_INFO,
           "CCXPREAUTHNOTIFY=" MAC_ADDRESS_STR" %d:%d",
           MAC_ADDR_ARRAY(pRoamInfo->bssid),
           pRoamInfo->timestamp[0],
           pRoamInfo->timestamp[1]);

    nBytes = snprintf(pos, freeBytes, "CCXPREAUTHNOTIFY=");
    pos += nBytes;
    freeBytes -= nBytes;

    vos_mem_copy(pos, pRoamInfo->bssid, VOS_MAC_ADDR_SIZE);
    pos += VOS_MAC_ADDR_SIZE;
    freeBytes -= VOS_MAC_ADDR_SIZE;

    nBytes = snprintf(pos, freeBytes, " %u:%u",
             pRoamInfo->timestamp[0], pRoamInfo->timestamp[1]);
    freeBytes -= nBytes;

    wrqu.data.pointer = buf;
    wrqu.data.length = (IW_CUSTOM_MAX - freeBytes);

    // send the event
    wireless_send_event(pAdapter->dev, IWEVCUSTOM, &wrqu, buf);
}

static void
hdd_indicateEseAdjApRepInd(hdd_adapter_t *pAdapter, tCsrRoamInfo *pRoamInfo)
{
    union iwreq_data wrqu;
    char buf[IW_CUSTOM_MAX + 1];
    int nBytes = 0;

    if ((NULL == pAdapter) || (NULL == pRoamInfo))
        return;

    // create the event
    memset(&wrqu, '\0', sizeof(wrqu));
    memset(buf, '\0', sizeof(buf));

    hddLog(VOS_TRACE_LEVEL_INFO, "CCXADJAPREP=%u", pRoamInfo->tsmRoamDelay);

    nBytes = snprintf(buf, IW_CUSTOM_MAX, "CCXADJAPREP=%u", pRoamInfo->tsmRoamDelay);

    wrqu.data.pointer = buf;
    wrqu.data.length = nBytes;

    // send the event
    wireless_send_event(pAdapter->dev, IWEVCUSTOM, &wrqu, buf);
}

void hdd_indicateEseBcnReportNoResults(const hdd_adapter_t *pAdapter,
                                       const tANI_U16 measurementToken,
                                       const tANI_BOOLEAN flag,
                                       const tANI_U8 numBss)
{
    union iwreq_data wrqu;
    char buf[IW_CUSTOM_MAX];
    char *pos = buf;
    int nBytes = 0, freeBytes = IW_CUSTOM_MAX;

    memset(&wrqu, '\0', sizeof(wrqu));
    memset(buf, '\0', sizeof(buf));

    hddLog(VOS_TRACE_LEVEL_INFO, FL("CCXBCNREP=%d %d %d"), measurementToken, flag,
           numBss);

    nBytes = snprintf(pos, freeBytes, "CCXBCNREP=%d %d %d", measurementToken,
                      flag, numBss);

    wrqu.data.pointer = buf;
    wrqu.data.length = nBytes;
    // send the event
    wireless_send_event(pAdapter->dev, IWEVCUSTOM, &wrqu, buf);
}

static void
hdd_indicateEseBcnReportInd(const hdd_adapter_t *pAdapter,
                            const tCsrRoamInfo *pRoamInfo)
{
    union iwreq_data wrqu;
    char buf[IW_CUSTOM_MAX];
    char *pos = buf;
    int nBytes = 0, freeBytes = IW_CUSTOM_MAX;
    tANI_U8 i = 0, len = 0;
    tANI_U8 tot_bcn_ieLen = 0;  /* total size of the beacon report data */
    tANI_U8 lastSent = 0, sendBss = 0;
    int bcnRepFieldSize = sizeof(pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[0].bcnReportFields);
    tANI_U8 ieLenByte = 1;
    /* CCXBCNREP=meas_tok<sp>flag<sp>no_of_bss<sp>tot_bcn_ie_len = 18 bytes */
#define ESEBCNREPHEADER_LEN  (18)

    if ((NULL == pAdapter) || (NULL == pRoamInfo))
        return;

    /* Custom event can pass maximum of 256 bytes of data,
       based on the IE len we need to identify how many BSS info can
       be filled in to custom event data */
    /*
       meas_tok<sp>flag<sp>no_of_bss<sp>tot_bcn_ie_len bcn_rep_data
         bcn_rep_data will have bcn_rep_fields,ie_len,ie without any spaces
         CCXBCNREP=meas_tok<sp>flag<sp>no_of_bss<sp>tot_bcn_ie_len = 18 bytes
    */

    if ((pRoamInfo->pEseBcnReportRsp->flag >> 1) && (!pRoamInfo->pEseBcnReportRsp->numBss))
    {
        hddLog(VOS_TRACE_LEVEL_INFO, "Measurement Done but no scan results");
        /* If the measurement is none and no scan results found,
            indicate the supplicant about measurement done */
        hdd_indicateEseBcnReportNoResults(pAdapter,
                                 pRoamInfo->pEseBcnReportRsp->measurementToken,
                                 pRoamInfo->pEseBcnReportRsp->flag,
            pRoamInfo->pEseBcnReportRsp->numBss);
    }
    else
    {
        while (lastSent < pRoamInfo->pEseBcnReportRsp->numBss)
        {
            memset(&wrqu, '\0', sizeof(wrqu));
            memset(buf, '\0', sizeof(buf));
            tot_bcn_ieLen = 0;
            sendBss = 0;
            pos = buf;
            freeBytes = IW_CUSTOM_MAX;

            for (i = lastSent; i < pRoamInfo->pEseBcnReportRsp->numBss; i++)
            {
                len = bcnRepFieldSize + ieLenByte + pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i].ieLen;
                if ((len + tot_bcn_ieLen) > (IW_CUSTOM_MAX - ESEBCNREPHEADER_LEN))
                {
                    break;
                }
                tot_bcn_ieLen += len;
                sendBss++;
                hddLog(VOS_TRACE_LEVEL_INFO, "i(%d) sizeof bcnReportFields(%d)"
                             "IeLength(%d) Length of Ie(%d) totLen(%d)",
                              i, bcnRepFieldSize, 1,
                              pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i].ieLen,
                              tot_bcn_ieLen);
            }

            hddLog(VOS_TRACE_LEVEL_INFO, "Sending %d BSS Info", sendBss);
            hddLog(VOS_TRACE_LEVEL_INFO, "CCXBCNREP=%d %d %d %d",
                pRoamInfo->pEseBcnReportRsp->measurementToken, pRoamInfo->pEseBcnReportRsp->flag,
                sendBss, tot_bcn_ieLen);

            nBytes = snprintf(pos, freeBytes, "CCXBCNREP=%d %d %d ",
                pRoamInfo->pEseBcnReportRsp->measurementToken, pRoamInfo->pEseBcnReportRsp->flag,
                sendBss);
            pos += nBytes;
            freeBytes -= nBytes;

            /* Copy total Beacon report data length */
            vos_mem_copy(pos, (char*)&tot_bcn_ieLen, sizeof(tot_bcn_ieLen));
            pos += sizeof(tot_bcn_ieLen);
            freeBytes -= sizeof(tot_bcn_ieLen);

            for (i = 0; i < sendBss; i++)
            {
                hddLog(VOS_TRACE_LEVEL_INFO, "ChanNum(%d) Spare(%d) MeasDuration(%d)"
                       " PhyType(%d) RecvSigPower(%d) ParentTSF(%u)"
                       " TargetTSF[0](%u) TargetTSF[1](%u) BeaconInterval(%u)"
                       " CapabilityInfo(%d) BSSID(%02X:%02X:%02X:%02X:%02X:%02X)",
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.ChanNum,
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.Spare,
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.MeasDuration,
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.PhyType,
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.RecvSigPower,
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.ParentTsf,
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.TargetTsf[0],
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.TargetTsf[1],
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.BcnInterval,
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.CapabilityInfo,
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.Bssid[0],
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.Bssid[1],
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.Bssid[2],
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.Bssid[3],
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.Bssid[4],
                        pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields.Bssid[5]);

                /* bcn report fields are copied */
                len = sizeof(pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields);
                vos_mem_copy(pos, (char*)&pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].bcnReportFields, len);
                pos += len;
                freeBytes -= len;

                /* Add 1 byte of ie len */
                len = pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].ieLen;
                vos_mem_copy(pos, (char*)&len, sizeof(len));
                pos += sizeof(len);
                freeBytes -= sizeof(len);

                /* copy IE from scan results */
                vos_mem_copy(pos, (char*)pRoamInfo->pEseBcnReportRsp->bcnRepBssInfo[i+lastSent].pBuf, len);
                pos += len;
                freeBytes -= len;
            }

            wrqu.data.pointer = buf;
            wrqu.data.length = IW_CUSTOM_MAX - freeBytes;

            // send the event
            wireless_send_event(pAdapter->dev, IWEVCUSTOM, &wrqu, buf);
            lastSent += sendBss;
        }
    }
}

#endif /* FEATURE_WLAN_ESE && FEATURE_WLAN_ESE_UPLOAD */


eHalStatus
hdd_smeRoamCallback(void *pContext, tCsrRoamInfo *pRoamInfo, tANI_U32 roamId,
                    eRoamCmdStatus roamStatus, eCsrRoamResult roamResult)
{
    eHalStatus halStatus = eHAL_STATUS_SUCCESS;
    hdd_adapter_t *pAdapter = (hdd_adapter_t *)pContext;
    hdd_wext_state_t *pWextState = NULL;
    hdd_station_ctx_t *pHddStaCtx = NULL;
    VOS_STATUS status = VOS_STATUS_SUCCESS;
    hdd_context_t *pHddCtx = NULL;
    struct cfg80211_bss *bss_status;
    tHalHandle hal;
    tpAniSirGlobal mac;
    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
            "CSR Callback: status= %d result= %d roamID=%d",
                    roamStatus, roamResult, roamId );

    /* Sanity check */
    if ((NULL == pAdapter) || (WLAN_HDD_ADAPTER_MAGIC != pAdapter->magic)) {
       VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_FATAL,
          "invalid adapter or adapter has invalid magic");
       return eHAL_STATUS_FAILURE;
    }

    pWextState = WLAN_HDD_GET_WEXT_STATE_PTR(pAdapter);
    pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);

    MTRACE(vos_trace(VOS_MODULE_ID_HDD, TRACE_CODE_HDD_RX_SME_MSG,
                          pAdapter->sessionId, roamStatus));
    switch( roamStatus )
    {
        case eCSR_ROAM_SESSION_OPENED:
            set_bit(SME_SESSION_OPENED, &pAdapter->event_flags);
            complete(&pAdapter->session_open_comp_var);
            hddLog(LOG1, FL("session %d opened"), pAdapter->sessionId);
            break;

#if  defined (WLAN_FEATURE_VOWIFI_11R) || defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR)
            /* We did pre-auth,then we attempted a 11r or ese reassoc.
             * reassoc failed due to failure, timeout, reject from ap
             * in any case tell the OS, our carrier is off and mark
             * interface down */
        case eCSR_ROAM_FT_REASSOC_FAILED:
            hddLog(LOGE, FL("Reassoc Failed with roamStatus: %d roamResult: %d SessionID: %d"),
                   roamStatus, roamResult, pAdapter->sessionId);
            halStatus = hdd_DisConnectHandler( pAdapter, pRoamInfo, roamId, roamStatus, roamResult );
            /* Check if Mcast/Bcast Filters are set, if yes clear the filters here */
            if ((WLAN_HDD_GET_CTX(pAdapter))->hdd_mcastbcast_filter_set == TRUE) {
                    (WLAN_HDD_GET_CTX(pAdapter))->hdd_mcastbcast_filter_set = FALSE;
            }
            pHddStaCtx->ft_carrier_on = FALSE;
            pHddStaCtx->hdd_ReassocScenario = FALSE;
            hddLog(LOG1,
                   FL("hdd_ReassocScenario set to: %d, ReAssoc Failed, session: %d"),
                   pHddStaCtx->hdd_ReassocScenario, pAdapter->sessionId);
            break;

        case eCSR_ROAM_FT_START:
            // When we roam for ESE and 11r, we dont want the
            // OS to be informed that the link is down. So mark
            // the link ready for ft_start. After this the
            // eCSR_ROAM_SHOULD_ROAM will be received.
            // Where in we will not mark the link down
            // Also we want to stop tx at this point when we will be
            // doing disassoc at this time. This saves 30-60 msec
            // after reassoc.
            {
                hddLog(LOG1, FL("Disabling queues"));
                wlan_hdd_netif_queue_control(pAdapter, WLAN_NETIF_TX_DISABLE,
                            WLAN_CONTROL_PATH);
                /*
                 * Deregister for this STA with TL with the objective to flush
                 * all the packets for this STA from wmm_tx_queue. If not done here,
                 * we would run into a race condition (CR390567) wherein TX
                 * thread would schedule packets from wmm_tx_queue AFTER peer STA has
                 * been deleted. And, these packets get assigned with a STA idx of
                 * self-sta (since the peer STA has been deleted) and get transmitted
                 * on the new channel before the reassoc request. Since there will be
                 * no ACK on the new channel, each packet gets retransmitted which
                 * takes several seconds before the transmission of reassoc request.
                 * This leads to reassoc-timeout and roam failure.
                 */
                status = hdd_roamDeregisterSTA( pAdapter, pHddStaCtx->conn_info.staId [0] );
                if ( !VOS_IS_STATUS_SUCCESS(status ) )
                {
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_WARN,
                            FL("hdd_roamDeregisterSTA() failed to for staID %d.  Status= %d [0x%x]"),
                            pHddStaCtx->conn_info.staId[0], status, status );
                    halStatus = eHAL_STATUS_FAILURE;
                }
            }
            pHddStaCtx->ft_carrier_on = TRUE;
            pHddStaCtx->hdd_ReassocScenario = VOS_TRUE;
            hddLog(LOG1,
                   FL("hdd_ReassocScenario set to: %d due to eCSR_ROAM_FT_START, session: %d"),
                   pHddStaCtx->hdd_ReassocScenario, pAdapter->sessionId);
            break;
#endif

        case eCSR_ROAM_SHOULD_ROAM:
           // Dont need to do anything
            {
                hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
                // notify apps that we can't pass traffic anymore
                hddLog(LOG1, FL("Disabling queues"));
                wlan_hdd_netif_queue_control(pAdapter,
                    WLAN_NETIF_TX_DISABLE,
                    WLAN_CONTROL_PATH);
#if  defined (WLAN_FEATURE_VOWIFI_11R) || defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR)
                if (pHddStaCtx->ft_carrier_on == FALSE)
                {
#endif
                    wlan_hdd_netif_queue_control(pAdapter,
                        WLAN_NETIF_CARRIER_OFF,
                        WLAN_CONTROL_PATH);
#if  defined (WLAN_FEATURE_VOWIFI_11R) || defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR)
                }
#endif

#if  !(defined (WLAN_FEATURE_VOWIFI_11R) || defined (FEATURE_WLAN_ESE) || defined(FEATURE_WLAN_LFR))
                //We should clear all sta register with TL, for now, only one.
                status = hdd_roamDeregisterSTA( pAdapter, pHddStaCtx->conn_info.staId [0] );
                if ( !VOS_IS_STATUS_SUCCESS(status ) )
                {
                    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_WARN,
                        FL("hdd_roamDeregisterSTA() failed to for staID %d.  Status= %d [0x%x]"),
                                        pHddStaCtx->conn_info.staId[0], status, status );
                    halStatus = eHAL_STATUS_FAILURE;
                }
#endif
            }
           break;
        case eCSR_ROAM_LOSTLINK:
            if(roamResult == eCSR_ROAM_RESULT_LOSTLINK) {
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO_HIGH,
                          "Roaming started due to connection lost");
                hddLog(LOG1, FL("Disabling queues"));
                wlan_hdd_netif_queue_control(pAdapter,
                    WLAN_NETIF_TX_DISABLE_N_CARRIER,
                    WLAN_CONTROL_PATH);
                break;
            }
        case eCSR_ROAM_DISASSOCIATED:
            {
                VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                        "****eCSR_ROAM_DISASSOCIATED****");
                halStatus = hdd_DisConnectHandler( pAdapter, pRoamInfo, roamId, roamStatus, roamResult );
                /* Check if Mcast/Bcast Filters are set, if yes clear the filters here */
                pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
                if (pHddCtx->hdd_mcastbcast_filter_set == TRUE)
                {
                    hdd_conf_mcastbcast_filter(pHddCtx, FALSE);

                    if (VOS_TRUE == pHddCtx->sus_res_mcastbcast_filter_valid) {
                        pHddCtx->configuredMcastBcastFilter =
                            pHddCtx->sus_res_mcastbcast_filter;
                        pHddCtx->sus_res_mcastbcast_filter_valid = VOS_FALSE;
                    }

                    hddLog(VOS_TRACE_LEVEL_INFO,
                           "offload: disassociation happening, restoring configuredMcastBcastFilter");
                    hddLog(VOS_TRACE_LEVEL_INFO,"McastBcastFilter = %d",
                           pHddCtx->configuredMcastBcastFilter);
                    hddLog(VOS_TRACE_LEVEL_INFO,
                           "offload: already called mcastbcast filter");
                    (WLAN_HDD_GET_CTX(pAdapter))->hdd_mcastbcast_filter_set = FALSE;
                }
#ifdef WLAN_FEATURE_PACKET_FILTERING
                /* Call to clear any MC Addr List filter applied after
                 * successful connection.
                 */
                if (wlan_hdd_set_mc_addr_list(pAdapter, FALSE))
                    hddLog(VOS_TRACE_LEVEL_ERROR, FL("failed to clear mc addr list"));
#endif
            }
            break;
        case eCSR_ROAM_IBSS_LEAVE:
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                    "****eCSR_ROAM_IBSS_LEAVE****");
            halStatus = hdd_DisConnectHandler( pAdapter, pRoamInfo, roamId, roamStatus, roamResult );
            break;
        case eCSR_ROAM_ASSOCIATION_COMPLETION:
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                    "****eCSR_ROAM_ASSOCIATION_COMPLETION****");
            // To Do - address probable memory leak with WEP encryption upon successful association
            if (eCSR_ROAM_RESULT_ASSOCIATED != roamResult)
            {
               //Clear saved connection information in HDD
               hdd_connRemoveConnectInfo( WLAN_HDD_GET_STATION_CTX_PTR(pAdapter) );
            }
            halStatus = hdd_AssociationCompletionHandler( pAdapter, pRoamInfo, roamId, roamStatus, roamResult );
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
            if (pRoamInfo)
                pRoamInfo->roamSynchInProgress = VOS_FALSE;
#endif
            break;
        case eCSR_ROAM_ASSOCIATION_FAILURE:
            halStatus = hdd_AssociationCompletionHandler( pAdapter,
                    pRoamInfo, roamId, roamStatus, roamResult );
            break;
        case eCSR_ROAM_IBSS_IND:
            hdd_RoamIbssIndicationHandler( pAdapter, pRoamInfo, roamId,
                                           roamStatus, roamResult );
            break;

        case eCSR_ROAM_CONNECT_STATUS_UPDATE:
            halStatus = roamRoamConnectStatusUpdateHandler( pAdapter, pRoamInfo, roamId, roamStatus, roamResult );
            break;

        case eCSR_ROAM_MIC_ERROR_IND:
            halStatus = hdd_RoamMicErrorIndicationHandler( pAdapter, pRoamInfo, roamId, roamStatus, roamResult );
            break;

        case eCSR_ROAM_SET_KEY_COMPLETE:
            {
                hdd_context_t* pHddCtx = (hdd_context_t*)pAdapter->pHddCtx;

                if((pHddCtx) &&
                   (VOS_TRUE == pHddStaCtx->hdd_ReassocScenario) &&
                   (TRUE == pHddCtx->hdd_wlan_suspended) &&
                   ((eCSR_ROAM_RESULT_NONE == roamResult)||
                     (pRoamInfo && pRoamInfo->is11rAssoc))) {
                    /* Send DTIM period to the FW; only if the wlan is already
                       in suspend. This is the case with roaming (reassoc),
                       DELETE_BSS_REQ zeroes out Modulated/Dynamic DTIM sent in
                       previous suspend_wlan. Sending SET_POWER_PARAMS_REQ
                       before the ENTER_BMPS_REQ ensures Listen Interval is
                       regained back to LI * Modulated DTIM */
                    hdd_set_pwrparams(pHddCtx);

                    /* At this point, device should not be in BMPS;
                       if due to unexpected scenario, if we are in BMPS,
                       then trigger Exit and Enter BMPS to take DTIM period
                       effective */
                    if (BMPS == pmcGetPmcState(pHddCtx->hHal))
                    {
                        hddLog( LOGE, FL("Not expected: device is already in BMPS mode, Exit & Enter BMPS again!"));

                        sme_RequestFullPower(WLAN_HDD_GET_HAL_CTX(pAdapter),
                                         iw_full_power_cbfn, pAdapter,
                                         eSME_FULL_PWR_NEEDED_BY_HDD);
                    }
                }
                if ((pHddCtx) &&
                    (FULL_POWER == pmcGetPmcState(pHddCtx->hHal)) &&
                    (VOS_TRUE == pHddStaCtx->hdd_ReassocScenario) &&
                    ((eCSR_ROAM_RESULT_NONE == roamResult) ||
                      (pRoamInfo && pRoamInfo->is11rAssoc))) {
                     hddLog(LOG1,
                           FL("Device in full power, Stop and start traffic timer for roaming"));
                     pmcStopTrafficTimer(pHddCtx->hHal);

                     if (pmcStartTrafficTimer(pHddCtx->hHal,
                         TRAFFIC_TIMER_ROAMING) != eHAL_STATUS_SUCCESS)
                         hddLog(LOGP, FL("Cannot start traffic timer"));
                }
                halStatus = hdd_RoamSetKeyCompleteHandler( pAdapter, pRoamInfo, roamId, roamStatus, roamResult );
                if ((eCSR_ROAM_RESULT_NONE == roamResult) ||
                     (pRoamInfo && pRoamInfo->is11rAssoc)) {
                    pHddStaCtx->hdd_ReassocScenario = VOS_FALSE;
                    hddLog(LOG1,
                           FL("hdd_ReassocScenario set to: %d, set key complete, session: %d"),
                           pHddStaCtx->hdd_ReassocScenario,
                           pAdapter->sessionId);
                }
            }
#ifdef WLAN_FEATURE_ROAM_OFFLOAD
            if (pRoamInfo != NULL)
                pRoamInfo->roamSynchInProgress = VOS_FALSE;
#endif
            break;
#ifdef WLAN_FEATURE_VOWIFI_11R
        case eCSR_ROAM_FT_RESPONSE:
            hdd_SendFTEvent(pAdapter);
            break;
#endif
#if defined(FEATURE_WLAN_LFR) && (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)) \
	    || defined(WITH_BACKPORTS)
        case eCSR_ROAM_PMK_NOTIFY:
           if (eCSR_AUTH_TYPE_RSN == pHddStaCtx->conn_info.authType
#ifdef WLAN_FEATURE_11W
               || (eCSR_AUTH_TYPE_RSN_8021X_SHA256 ==
                                 pHddStaCtx->conn_info.authType)
#endif
                                                              )
           {
               /* Notify the supplicant of a new candidate */
               halStatus = wlan_hdd_cfg80211_pmksa_candidate_notify(pAdapter, pRoamInfo, 1, false);
           }
           break;
#endif

#ifdef FEATURE_WLAN_LFR_METRICS
        case eCSR_ROAM_PREAUTH_INIT_NOTIFY:
           /* This event is to notify pre-auth initiation */
           if (VOS_STATUS_SUCCESS !=
               wlan_hdd_cfg80211_roam_metrics_preauth(pAdapter, pRoamInfo))
           {
               halStatus = eHAL_STATUS_FAILURE;
           }
           break;
        case eCSR_ROAM_PREAUTH_STATUS_SUCCESS:
           /* This event will notify pre-auth completion in case of success */
           if (VOS_STATUS_SUCCESS !=
               wlan_hdd_cfg80211_roam_metrics_preauth_status(pAdapter,
                                                             pRoamInfo, 1))
           {
               halStatus = eHAL_STATUS_FAILURE;
           }
           break;
        case eCSR_ROAM_PREAUTH_STATUS_FAILURE:
           /* This event will notify pre-auth completion in case of failure. */
           if (VOS_STATUS_SUCCESS !=
               wlan_hdd_cfg80211_roam_metrics_preauth_status(pAdapter,
                                                             pRoamInfo, 0))
           {
               halStatus = eHAL_STATUS_FAILURE;
           }
           break;
        case eCSR_ROAM_HANDOVER_SUCCESS:
           /* This event is to notify handover success.
              It will be only invoked on success */
           if (VOS_STATUS_SUCCESS !=
               wlan_hdd_cfg80211_roam_metrics_handover(pAdapter, pRoamInfo))
           {
               halStatus = eHAL_STATUS_FAILURE;
           }
           break;
#endif
        case eCSR_ROAM_REMAIN_CHAN_READY:
            hdd_remainChanReadyHandler( pAdapter );
            break;
#ifdef FEATURE_WLAN_TDLS
        case eCSR_ROAM_TDLS_STATUS_UPDATE:
            halStatus = hdd_RoamTdlsStatusUpdateHandler( pAdapter, pRoamInfo,
                                                roamId, roamStatus, roamResult );
            break ;
        case eCSR_ROAM_RESULT_MGMT_TX_COMPLETE_IND:
            wlan_hdd_tdls_mgmt_completion_callback(pAdapter, pRoamInfo->reasonCode);
            break;

#endif
#ifdef WLAN_FEATURE_11W
       case eCSR_ROAM_UNPROT_MGMT_FRAME_IND:
            hdd_indicateUnprotMgmtFrame(pAdapter, pRoamInfo->nFrameLength,
                                         pRoamInfo->pbFrames,
                                         pRoamInfo->frameType);
            break;
#endif
#if defined(FEATURE_WLAN_ESE) && defined(FEATURE_WLAN_ESE_UPLOAD)
       case eCSR_ROAM_TSM_IE_IND:
            hdd_indicateTsmIe(pAdapter, pRoamInfo->tsmIe.tsid,
                pRoamInfo->tsmIe.state, pRoamInfo->tsmIe.msmt_interval);
           break;

       case eCSR_ROAM_CCKM_PREAUTH_NOTIFY:
          {
              if (eCSR_AUTH_TYPE_CCKM_WPA == pHddStaCtx->conn_info.authType ||
                  eCSR_AUTH_TYPE_CCKM_RSN == pHddStaCtx->conn_info.authType)
              {
                  hdd_indicateCckmPreAuth(pAdapter, pRoamInfo);
              }
              break;
          }

       case eCSR_ROAM_ESE_ADJ_AP_REPORT_IND:
         {
             hdd_indicateEseAdjApRepInd(pAdapter, pRoamInfo);
             break;
         }

       case eCSR_ROAM_ESE_BCN_REPORT_IND:
         {
            hdd_indicateEseBcnReportInd(pAdapter, pRoamInfo);
            break;
         }
#endif /* FEATURE_WLAN_ESE && FEATURE_WLAN_ESE_UPLOAD */
      case eCSR_ROAM_UPDATE_SCAN_RESULT:
         if ((NULL != pRoamInfo) && (NULL != pRoamInfo->pBssDesc)) {
              bss_status = wlan_hdd_cfg80211_inform_bss_frame(pAdapter,
                                pRoamInfo->pBssDesc);
              if (NULL == bss_status)
                  hddLog(VOS_TRACE_LEVEL_INFO,
                            FL("UPDATE_SCAN_RESULT returned NULL"));
              else
                  cfg80211_put_bss(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)) || defined(WITH_BACKPORTS)
                                   (WLAN_HDD_GET_CTX(pAdapter))->wiphy,
#endif
                                   bss_status);
         }
         break;
        case eCSR_ROAM_NDP_STATUS_UPDATE:
            hdd_ndp_event_handler(pAdapter, pRoamInfo, roamId, roamStatus,
                roamResult );
            break;
        case eCSR_ROAM_SAE_COMPUTE:
            if (pRoamInfo)
                wlan_hdd_sae_callback(pAdapter, pRoamInfo);
        break;
        case  eCSR_ROAM_STA_CHANNEL_SWITCH:
	  {
		pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
		if((pHddCtx) && (pHddCtx->hHal) && (pRoamInfo)) {
			hal = pHddCtx->hHal;
			mac = PMAC_STRUCT(hal);
			hdd_chan_change_notify(pAdapter, pAdapter->dev,
				               pRoamInfo->chan_info.chan_id,
					       mac->roam.configParam.phyMode);
		  }
	  }
        default:
            break;
    }
    return( halStatus );
}

#ifdef WLAN_FEATURE_FILS_SK
/**
 * hdd_translate_fils_rsn_to_csr_auth() - Translate FILS RSN to CSR auth type
 * @auth_suite: auth suite
 * @auth_type: pointer to eCsrAuthType
 *
 * Return: None
 */
static void hdd_translate_fils_rsn_to_csr_auth(u_int8_t auth_suite[4],
					eCsrAuthType *auth_type)
{
	if (!memcmp(auth_suite, ccp_rsn_oui_0e, 4))
		*auth_type = eCSR_AUTH_TYPE_FILS_SHA256;
	else if (!memcmp(auth_suite, ccp_rsn_oui_0f, 4))
		*auth_type = eCSR_AUTH_TYPE_FILS_SHA384;
	else if (!memcmp(auth_suite, ccp_rsn_oui_10, 4))
		*auth_type = eCSR_AUTH_TYPE_FT_FILS_SHA256;
	else if (!memcmp(auth_suite, ccp_rsn_oui_11, 4))
		*auth_type = eCSR_AUTH_TYPE_FT_FILS_SHA384;
}
#else
static inline void hdd_translate_fils_rsn_to_csr_auth(u_int8_t auth_suite[4],
					eCsrAuthType *auth_type)
{
}
#endif

#ifdef WLAN_FEATURE_SAE
/**
 * hdd_translate_sae_rsn_to_csr_auth() - Translate SAE RSN to CSR auth type
 * @auth_suite: auth suite
 * @auth_type: pointer to eCsrAuthType
 *
 * Return: None
 */
static void hdd_translate_sae_rsn_to_csr_auth(int8_t auth_suite[4],
					      eCsrAuthType *auth_type)
{
	if (!memcmp(auth_suite, ccp_rsn_oui_80, 4))
		*auth_type = eCSR_AUTH_TYPE_SAE;
}
#else
static inline void hdd_translate_sae_rsn_to_csr_auth(int8_t auth_suite[4],
						     eCsrAuthType *auth_type)
{
}
#endif

eCsrAuthType hdd_TranslateRSNToCsrAuthType( u_int8_t auth_suite[4])
{
    eCsrAuthType auth_type = eCSR_AUTH_TYPE_UNKNOWN;
    // is the auth type supported?
    if ( memcmp(auth_suite , ccpRSNOui01, 4) == 0)
    {
        auth_type = eCSR_AUTH_TYPE_RSN;
    } else
    if (memcmp(auth_suite , ccpRSNOui02, 4) == 0)
    {
        auth_type = eCSR_AUTH_TYPE_RSN_PSK;
    } else
#ifdef WLAN_FEATURE_VOWIFI_11R
    if (memcmp(auth_suite , ccpRSNOui04, 4) == 0)
    {
        // Check for 11r FT Authentication with PSK
        auth_type = eCSR_AUTH_TYPE_FT_RSN_PSK;
    } else
    if (memcmp(auth_suite , ccpRSNOui03, 4) == 0)
    {
        // Check for 11R FT Authentication with 802.1X
        auth_type = eCSR_AUTH_TYPE_FT_RSN;
    } else
#endif
#ifdef FEATURE_WLAN_ESE
    if (memcmp(auth_suite , ccpRSNOui06, 4) == 0)
    {
        auth_type = eCSR_AUTH_TYPE_CCKM_RSN;
    } else
#endif /* FEATURE_WLAN_ESE */
#ifdef WLAN_FEATURE_11W
    if (memcmp(auth_suite , ccpRSNOui07, 4) == 0)
    {
        auth_type = eCSR_AUTH_TYPE_RSN_PSK_SHA256;
    } else
    if (memcmp(auth_suite , ccpRSNOui08, 4) == 0)
    {
        auth_type = eCSR_AUTH_TYPE_RSN_8021X_SHA256;
    } else if (memcmp(auth_suite, ccp_rsn_oui_18, 4) == 0) {
        auth_type = eCSR_AUTH_TYPE_OWE;
    } else
#endif
    {
        /* If auth suite is of fils, auth_type will be
         * overwritten in hdd_translate_fils_rsn_to_csr_auth
         */
        hdd_translate_fils_rsn_to_csr_auth(auth_suite, &auth_type);
        hdd_translate_sae_rsn_to_csr_auth(auth_suite, &auth_type);
    }

    return auth_type;
}

eCsrAuthType
hdd_TranslateWPAToCsrAuthType(u_int8_t auth_suite[4])
{
    eCsrAuthType auth_type = eCSR_AUTH_TYPE_UNKNOWN;
    // is the auth type supported?
    if ( memcmp(auth_suite , ccpWpaOui01, 4) == 0)
    {
        auth_type = eCSR_AUTH_TYPE_WPA;
    } else
    if (memcmp(auth_suite , ccpWpaOui02, 4) == 0)
    {
        auth_type = eCSR_AUTH_TYPE_WPA_PSK;
    } else
#ifdef FEATURE_WLAN_ESE
    if (memcmp(auth_suite , ccpWpaOui06, 4) == 0)
    {
        auth_type = eCSR_AUTH_TYPE_CCKM_WPA;
    } else
#endif /* FEATURE_WLAN_ESE */
	/* If auth suite is of fils, auth_type will be
	 * overwritten in hdd_translate_fils_rsn_to_csr_auth
	 */
	hdd_translate_fils_rsn_to_csr_auth(auth_suite, &auth_type);

    hddLog(LOG1, FL("auth_type: %d"), auth_type);
    return auth_type;
}

eCsrEncryptionType
hdd_TranslateRSNToCsrEncryptionType(u_int8_t cipher_suite[4])
{
    eCsrEncryptionType cipher_type;
    // is the cipher type supported?
    if ( memcmp(cipher_suite , ccpRSNOui04, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_AES;
    }
    else if (memcmp(cipher_suite , ccpRSNOui02, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_TKIP;
    }
    else if (memcmp(cipher_suite , ccpRSNOui00, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_NONE;
    }
    else if (memcmp(cipher_suite , ccpRSNOui01, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_WEP40_STATICKEY;
    }
    else if (memcmp(cipher_suite , ccpRSNOui05, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_WEP104_STATICKEY;
    }
    else
    {
        cipher_type = eCSR_ENCRYPT_TYPE_FAILED;
    }
    hddLog(LOG1, FL("cipher_type: %d"), cipher_type);
    return cipher_type;
}
/* To find if the MAC address is NULL */
static tANI_U8 hdd_IsMACAddrNULL (tANI_U8 *macAddr, tANI_U8 length)
{
    int i;
    for (i = 0; i < length; i++)
    {
        if (0x00 != (macAddr[i]))
        {
            return FALSE;
        }
    }
    return TRUE;
} /****** end hdd_IsMACAddrNULL() ******/

eCsrEncryptionType
hdd_TranslateWPAToCsrEncryptionType(u_int8_t cipher_suite[4])
{
    eCsrEncryptionType cipher_type;
    // is the cipher type supported?
    if ( memcmp(cipher_suite , ccpWpaOui04, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_AES;
    } else
    if (memcmp(cipher_suite , ccpWpaOui02, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_TKIP;
    } else
    if (memcmp(cipher_suite , ccpWpaOui00, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_NONE;
    } else
    if (memcmp(cipher_suite , ccpWpaOui01, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_WEP40_STATICKEY;
    } else
    if (memcmp(cipher_suite , ccpWpaOui05, 4) == 0)
    {
        cipher_type = eCSR_ENCRYPT_TYPE_WEP104_STATICKEY;
    } else
    {
        cipher_type = eCSR_ENCRYPT_TYPE_FAILED;
    }
    hddLog(LOG1, FL("cipher_type: %d"), cipher_type);
    return cipher_type;
}

#ifdef WLAN_FEATURE_FILS_SK
/*
 * hdd_is_fils_connection: API to determine if connection is FILS
 * @adapter: hdd adapter
 *
 * Return: true if fils connection else false
*/
static inline bool hdd_is_fils_connection(hdd_adapter_t *adapter)
{
    hdd_wext_state_t *wext_state = WLAN_HDD_GET_WEXT_STATE_PTR(adapter);
    if (wext_state->roamProfile.fils_con_info)
        return wext_state->roamProfile.fils_con_info->is_fils_connection;

    return false;
}
#else
static inline bool hdd_is_fils_connection(hdd_adapter_t *adapter)
{
    return false;
}
#endif

static tANI_S32 hdd_ProcessGENIE(hdd_adapter_t *pAdapter,
                struct ether_addr *pBssid,
                eCsrEncryptionType *pEncryptType,
                eCsrEncryptionType *mcEncryptType,
                eCsrAuthType *pAuthType,
#ifdef WLAN_FEATURE_11W
                u_int8_t *pMfpRequired,
                u_int8_t *pMfpCapable,
#endif
                u_int16_t gen_ie_len,
                u_int8_t *gen_ie)
{
    tHalHandle halHandle = WLAN_HDD_GET_HAL_CTX(pAdapter);
    eHalStatus result;
    tDot11fIERSN dot11RSNIE = {0};
    tDot11fIEWPA dot11WPAIE = {0};
    tANI_U32 i;
    tANI_U32 status;
    tANI_U8 *pRsnIe;
    tANI_U16 RSNIeLen;
    tPmkidCacheInfo PMKIDCache[4]; // Local transfer memory
    v_BOOL_t updatePMKCache = FALSE;

    vos_mem_zero(PMKIDCache, sizeof PMKIDCache);
    /* Clear struct of tDot11fIERSN and tDot11fIEWPA specifically setting present
       flag to 0 */
    memset( &dot11WPAIE, 0 , sizeof(tDot11fIEWPA) );
    memset( &dot11RSNIE, 0 , sizeof(tDot11fIERSN) );

    // Type check
    if ( gen_ie[0] ==  DOT11F_EID_RSN)
    {
        // Validity checks
        if ((gen_ie_len < DOT11F_IE_RSN_MIN_LEN ) ||
                (gen_ie_len > DOT11F_IE_RSN_MAX_LEN) )
        {
            hddLog(LOGE, "%s: Invalid DOT11F RSN IE length :%d",
                   __func__, gen_ie_len);
            return -EINVAL;
        }
        // Skip past the EID byte and length byte
        pRsnIe = gen_ie + 2;
        RSNIeLen = gen_ie_len - 2;
        // Unpack the RSN IE
        status = sme_unpack_rsn_ie(halHandle,
                                   pRsnIe,
                                   RSNIeLen,
                                   &dot11RSNIE);
        if (DOT11F_FAILED(status))
        {
            hddLog(LOGE,
                       FL("Parse failure in hdd_ProcessGENIE (0x%08x)"),
                       status);
            return -EINVAL;
        }
        hddLog(LOG1, FL("%s: gp_cipher_suite_present: %d"),
                __func__, dot11RSNIE.gp_cipher_suite_present);

        // Copy out the encryption and authentication types
        hddLog(LOG1, FL("%s: pairwise cipher suite count: %d"),
                __func__, dot11RSNIE.pwise_cipher_suite_count );
        hddLog(LOG1, FL("%s: authentication suite count: %d"),
                __func__, dot11RSNIE.akm_suite_cnt);
        /*Here we have followed the apple base code,
          but probably I suspect we can do something different*/
        //dot11RSNIE.akm_suite_count
        // Just translate the FIRST one
        *pAuthType =  hdd_TranslateRSNToCsrAuthType(dot11RSNIE.akm_suite[0]);
        //dot11RSNIE.pwise_cipher_suite_count
        *pEncryptType = hdd_TranslateRSNToCsrEncryptionType(dot11RSNIE.pwise_cipher_suites[0]);
        //dot11RSNIE.gp_cipher_suite_count
        *mcEncryptType = hdd_TranslateRSNToCsrEncryptionType(dot11RSNIE.gp_cipher_suite);
#ifdef WLAN_FEATURE_11W
        *pMfpRequired = (dot11RSNIE.RSN_Cap[0] >> 6) & 0x1 ;
        *pMfpCapable = (dot11RSNIE.RSN_Cap[0] >> 7) & 0x1 ;
#endif
        // Set the PMKSA ID Cache for this interface
        for (i=0; i<dot11RSNIE.pmkid_count; i++)
        {
            if ( pBssid == NULL)
            {
                hddLog(LOGE, "%s: pBssid passed is NULL", __func__);
                break;
            }
            if ( hdd_IsMACAddrNULL( (u_char *) pBssid->ether_addr_octet , 6))
            {
                hddLog(LOGE, "%s: Invalid MAC adrr", __func__);
                break;
            }
            updatePMKCache = TRUE;
            // For right now, I assume setASSOCIATE() has passed in the bssid.
            vos_mem_copy(PMKIDCache[i].BSSID,
                            pBssid, ETHER_ADDR_LEN);
            vos_mem_copy(PMKIDCache[i].PMKID,
                            dot11RSNIE.pmkid[i],
                            CSR_RSN_PMKID_SIZE);
        }

        if (updatePMKCache)
        {
            // Calling csrRoamSetPMKIDCache to configure the PMKIDs into the cache
            hddLog(LOG1, FL("%s: Calling csrRoamSetPMKIDCache with cache entry %d."),
                                                                            __func__, i );
            // Finally set the PMKSA ID Cache in CSR
            result = sme_RoamSetPMKIDCache(halHandle,pAdapter->sessionId,
                                           PMKIDCache,
                                           dot11RSNIE.pmkid_count,
                                           FALSE);
        }
    }
    else if (gen_ie[0] == DOT11F_EID_WPA)
    {
        // Validity checks
        if ((gen_ie_len < DOT11F_IE_WPA_MIN_LEN ) ||
                    (gen_ie_len > DOT11F_IE_WPA_MAX_LEN))
        {
            hddLog(LOGE, "%s: Invalid DOT11F WPA IE length :%d",
                   __func__, gen_ie_len);
            return -EINVAL;
        }
        // Skip past the EID byte and length byte - and four byte WiFi OUI
        pRsnIe = gen_ie + 2 + 4;
        RSNIeLen = gen_ie_len - (2 + 4);
        // Unpack the WPA IE
        status = dot11fUnpackIeWPA((tpAniSirGlobal) halHandle,
                            pRsnIe,
                            RSNIeLen,
                            &dot11WPAIE);

        if (DOT11F_FAILED(status))
        {
            hddLog(LOGE,
                   FL("Parse failure in hdd_ProcessGENIE (0x%08x)"),
                   status);
            return -EINVAL;
        }

        // Copy out the encryption and authentication types
        hddLog(LOG1, FL("%s: WPA unicast cipher suite count: %d"),
               __func__, dot11WPAIE.unicast_cipher_count );
        hddLog(LOG1, FL("%s: WPA authentication suite count: %d"),
               __func__, dot11WPAIE.auth_suite_count);
        //dot11WPAIE.auth_suite_count
        // Just translate the FIRST one
        *pAuthType =  hdd_TranslateWPAToCsrAuthType(dot11WPAIE.auth_suites[0]);
        //dot11WPAIE.unicast_cipher_count
        *pEncryptType = hdd_TranslateWPAToCsrEncryptionType(dot11WPAIE.unicast_ciphers[0]);
        //dot11WPAIE.unicast_cipher_count
        *mcEncryptType = hdd_TranslateWPAToCsrEncryptionType(dot11WPAIE.multicast_cipher);
    }
    else
    {
        hddLog(LOGW, FL("gen_ie[0]: %d"), gen_ie[0]);
        return -EINVAL;
    }
    return 0;
}
int hdd_SetGENIEToCsr( hdd_adapter_t *pAdapter, eCsrAuthType *RSNAuthType)
{
    hdd_wext_state_t *pWextState = WLAN_HDD_GET_WEXT_STATE_PTR(pAdapter);
    v_U32_t status = 0;
    eCsrEncryptionType RSNEncryptType;
    eCsrEncryptionType mcRSNEncryptType;
#ifdef WLAN_FEATURE_11W
    u_int8_t RSNMfpRequired = 0;
    u_int8_t RSNMfpCapable = 0;
#endif
    struct ether_addr   bSsid;   // MAC address of assoc peer
    // MAC address of assoc peer
    // But, this routine is only called when we are NOT associated.
    vos_mem_copy(bSsid.ether_addr_octet,
            pWextState->roamProfile.BSSIDs.bssid,
            sizeof(bSsid.ether_addr_octet));
    if (pWextState->WPARSNIE[0] == DOT11F_EID_RSN || pWextState->WPARSNIE[0] == DOT11F_EID_WPA)
    {
        //continue
    }
    else
    {
        return 0;
    }
    // The actual processing may eventually be more extensive than this.
    // Right now, just consume any PMKIDs that are  sent in by the app.
    status = hdd_ProcessGENIE(pAdapter,
            &bSsid,   // MAC address of assoc peer
            &RSNEncryptType,
            &mcRSNEncryptType,
            RSNAuthType,
#ifdef WLAN_FEATURE_11W
            &RSNMfpRequired,
            &RSNMfpCapable,
#endif
            pWextState->WPARSNIE[1]+2,
            pWextState->WPARSNIE);
    if (status == 0)
    {
        // Now copy over all the security attributes you have parsed out
        pWextState->roamProfile.EncryptionType.numEntries = 1;
        pWextState->roamProfile.mcEncryptionType.numEntries = 1;

        pWextState->roamProfile.EncryptionType.encryptionType[0] = RSNEncryptType; // Use the cipher type in the RSN IE
        pWextState->roamProfile.mcEncryptionType.encryptionType[0] = mcRSNEncryptType;

        if ( (WLAN_HDD_IBSS == pAdapter->device_mode) &&
             ((eCSR_ENCRYPT_TYPE_AES == mcRSNEncryptType) ||
             (eCSR_ENCRYPT_TYPE_TKIP == mcRSNEncryptType)))
        {
           /*For wpa none supplicant sends the WPA IE with unicast cipher as
             eCSR_ENCRYPT_TYPE_NONE ,where as the multicast cipher as
             either AES/TKIP based on group cipher configuration
             mentioned in the wpa_supplicant.conf.*/

           /*Set the unicast cipher same as multicast cipher*/
           pWextState->roamProfile.EncryptionType.encryptionType[0]
                                                     = mcRSNEncryptType;
        }

#ifdef WLAN_FEATURE_11W
        hddLog( LOG1, FL("RSNMfpRequired = %d, RSNMfpCapable = %d"),
                                       RSNMfpRequired, RSNMfpCapable);
        pWextState->roamProfile.MFPRequired = RSNMfpRequired;
        pWextState->roamProfile.MFPCapable = RSNMfpCapable;
#endif
        hddLog( LOG1,
          FL("CSR AuthType = %d, EncryptionType = %d mcEncryptionType = %d"),
           *RSNAuthType, RSNEncryptType, mcRSNEncryptType);
    }
    return status;
}

#ifdef WLAN_FEATURE_FILS_SK
/**
 * hdd_check_fils_rsn_n_set_auth_type() - This API checks whether a give
 * auth type is fils if yes, sets it in profile.
 * @rsn_auth_type: auth type
 *
 * Return: true if FILS auth else false
 */
static bool hdd_check_fils_rsn_n_set_auth_type(tCsrRoamProfile *roam_profile,
			    eCsrAuthType rsn_auth_type)
{
	bool is_fils_rsn = false;

	if ((rsn_auth_type == eCSR_AUTH_TYPE_FILS_SHA256) ||
	   (rsn_auth_type == eCSR_AUTH_TYPE_FILS_SHA384) ||
	   (rsn_auth_type == eCSR_AUTH_TYPE_FT_FILS_SHA256) ||
	   (rsn_auth_type == eCSR_AUTH_TYPE_FT_FILS_SHA384))
		is_fils_rsn = true;
	if (is_fils_rsn)
		roam_profile->fils_con_info->akm_type = rsn_auth_type;

	return is_fils_rsn;
}
#endif

int hdd_set_csr_auth_type ( hdd_adapter_t  *pAdapter, eCsrAuthType RSNAuthType)
{
    hdd_wext_state_t *pWextState = WLAN_HDD_GET_WEXT_STATE_PTR(pAdapter);
    tCsrRoamProfile* pRoamProfile = &(pWextState->roamProfile);
    hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);

    pRoamProfile->AuthType.numEntries = 1;
    hddLog( LOG1,
        "%s: authType = %d RSNAuthType %d wpa_versions %d",
         __func__, pHddStaCtx->conn_info.authType, RSNAuthType,
         pWextState->wpaVersion);

    switch( pHddStaCtx->conn_info.authType)
    {
       case eCSR_AUTH_TYPE_OPEN_SYSTEM:
       case eCSR_AUTH_TYPE_AUTOSWITCH:
#ifdef FEATURE_WLAN_ESE
       case eCSR_AUTH_TYPE_CCKM_WPA:
       case eCSR_AUTH_TYPE_CCKM_RSN:
#endif
        if (pWextState->wpaVersion & IW_AUTH_WPA_VERSION_DISABLED) {

           pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_OPEN_SYSTEM ;
        } else
        if (pWextState->wpaVersion & IW_AUTH_WPA_VERSION_WPA) {

#ifdef FEATURE_WLAN_ESE
            if ((RSNAuthType == eCSR_AUTH_TYPE_CCKM_WPA) &&
                ((pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X)
                 == IW_AUTH_KEY_MGMT_802_1X)) {
                hddLog( LOG1, "%s: set authType to CCKM WPA. AKM also 802.1X.", __func__);
                pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_CCKM_WPA;
            } else
            if (RSNAuthType == eCSR_AUTH_TYPE_CCKM_WPA) {
                hddLog( LOG1, "%s: Last chance to set authType to CCKM WPA.", __func__);
                pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_CCKM_WPA;
            } else
#endif
            if((pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X)
                    == IW_AUTH_KEY_MGMT_802_1X) {
               pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_WPA;
            } else
            if ((pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_PSK)
                    == IW_AUTH_KEY_MGMT_PSK) {
               pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_WPA_PSK;
            } else {
               pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_WPA_NONE;
            }
        }
        if (pWextState->wpaVersion & IW_AUTH_WPA_VERSION_WPA2) {
#ifdef FEATURE_WLAN_ESE
            if ((RSNAuthType == eCSR_AUTH_TYPE_CCKM_RSN) &&
                ((pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X)
                 == IW_AUTH_KEY_MGMT_802_1X)) {
                hddLog( LOG1, "%s: set authType to CCKM RSN. AKM also 802.1X.", __func__);
                pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_CCKM_RSN;
            } else
            if (RSNAuthType == eCSR_AUTH_TYPE_CCKM_RSN) {
                hddLog( LOG1, "%s: Last chance to set authType to CCKM RSN.", __func__);
                pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_CCKM_RSN;
            } else
#endif

#ifdef WLAN_FEATURE_VOWIFI_11R
            if ((RSNAuthType == eCSR_AUTH_TYPE_FT_RSN) &&
                ((pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X)
                 == IW_AUTH_KEY_MGMT_802_1X)) {
               pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_FT_RSN;
            }else
            if ((RSNAuthType == eCSR_AUTH_TYPE_FT_RSN_PSK) &&
                ((pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_PSK)
                 == IW_AUTH_KEY_MGMT_PSK)) {
               pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_FT_RSN_PSK;
            } else
#endif

#ifdef WLAN_FEATURE_11W
            if (RSNAuthType == eCSR_AUTH_TYPE_RSN_PSK_SHA256) {
                pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_RSN_PSK_SHA256;
            } else
            if (RSNAuthType == eCSR_AUTH_TYPE_RSN_8021X_SHA256) {
                pRoamProfile->AuthType.authType[0] =
                                            eCSR_AUTH_TYPE_RSN_8021X_SHA256;
            } else
#endif
#ifdef WLAN_FEATURE_FILS_SK
            if (hdd_check_fils_rsn_n_set_auth_type(pRoamProfile, RSNAuthType)) {
				pRoamProfile->AuthType.authType[0] = RSNAuthType;
				hddLog(LOG1, "updated profile authtype as %d", RSNAuthType);
            } else
#endif
            if ((RSNAuthType == eCSR_AUTH_TYPE_SAE) &&
		((pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X)
		== IW_AUTH_KEY_MGMT_802_1X)) {
		/* SAE case */
		pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_SAE;
	    } else
            if ((RSNAuthType == eCSR_AUTH_TYPE_OWE) &&
                ((pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X)
                  == IW_AUTH_KEY_MGMT_802_1X)) {
               /* OWE case */
               pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_OWE;
            } else
            if( (pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X)
                    == IW_AUTH_KEY_MGMT_802_1X) {
               pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_RSN;
            } else
            if ( (pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_PSK)
                    == IW_AUTH_KEY_MGMT_PSK) {
               pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_RSN_PSK;
            } else {
               pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_UNKNOWN;
            }
        }
        break;

       case eCSR_AUTH_TYPE_SHARED_KEY:

          pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_SHARED_KEY;
          break;

       case eCSR_AUTH_TYPE_SAE:
          pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_SAE;
          break;

        default:

#ifdef FEATURE_WLAN_ESE
           hddLog( LOG1, "%s: In default, unknown auth type.", __func__);
#endif /* FEATURE_WLAN_ESE */
           pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_UNKNOWN;
           break;
    }

    hddLog( LOG1, "%s Set roam Authtype to %d",
            __func__, pWextState->roamProfile.AuthType.authType[0]);

    return 0;
}

/**
 * __iw_set_essid() - This function sets the ssid received from wpa_supplicant
 *			to the CSR roam profile.
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure
 */
static int __iw_set_essid(struct net_device *dev,
                        struct iw_request_info *info,
                        union iwreq_data *wrqu, char *extra)
{
    v_U32_t status = 0;
    hdd_wext_state_t *pWextState;
    hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    v_U32_t roamId;
    tCsrRoamProfile          *pRoamProfile;
    hdd_context_t *pHddCtx;
    eCsrAuthType RSNAuthType;
    uint16_t ch_width;
    tHalHandle hHal = WLAN_HDD_GET_HAL_CTX(pAdapter);
    int ret;

    ENTER();

    pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
    ret = wlan_hdd_validate_context(pHddCtx);
    if (0 != ret)
        return ret;

    pWextState = WLAN_HDD_GET_WEXT_STATE_PTR(pAdapter);

    if (pAdapter->device_mode != WLAN_HDD_INFRA_STATION &&
        pAdapter->device_mode != WLAN_HDD_P2P_CLIENT) {
        hddLog(LOGW, FL("Device mode %s(%d) is not allowed"),
               hdd_device_mode_to_string(pAdapter->device_mode),
               pAdapter->device_mode);
        return -EINVAL;
    }

    if(pWextState->mTKIPCounterMeasures == TKIP_COUNTER_MEASURE_STARTED) {
        hddLog(VOS_TRACE_LEVEL_INFO_HIGH, "%s :Counter measure is in progress", __func__);
        return -EBUSY;
    }
    if( SIR_MAC_MAX_SSID_LENGTH < wrqu->essid.length )
        return -EINVAL;

    pRoamProfile = &pWextState->roamProfile;

    /*Try disconnecting if already in connected state*/
    status = wlan_hdd_try_disconnect(pAdapter);
    if (0 > status)
    {
      hddLog(VOS_TRACE_LEVEL_ERROR, FL("Failed to disconnect the existing"
            " connection"));
      return -EALREADY;
    }
    /** when cfg80211 defined, wpa_supplicant wext driver uses
      zero-length, null-string ssid for force disconnection.
      after disconnection (if previously connected) and cleaning ssid,
      driver MUST return success */
    if ( 0 == wrqu->essid.length ) {
        return 0;
    }

    status = hdd_wmm_get_uapsd_mask(pAdapter,
                                    &pWextState->roamProfile.uapsd_mask);
    if (VOS_STATUS_SUCCESS != status)
    {
       pWextState->roamProfile.uapsd_mask = 0;
    }
    pWextState->roamProfile.SSIDs.numOfSSIDs = 1;

    pWextState->roamProfile.SSIDs.SSIDList->SSID.length = wrqu->essid.length;

    vos_mem_zero(pWextState->roamProfile.SSIDs.SSIDList->SSID.ssId, sizeof(pWextState->roamProfile.SSIDs.SSIDList->SSID.ssId));
    vos_mem_copy((void *)(pWextState->roamProfile.SSIDs.SSIDList->SSID.ssId), extra, wrqu->essid.length);
    if (IW_AUTH_WPA_VERSION_WPA == pWextState->wpaVersion ||
        IW_AUTH_WPA_VERSION_WPA2 == pWextState->wpaVersion ) {

        //set gen ie
        hdd_SetGENIEToCsr(pAdapter, &RSNAuthType);

        //set auth
        hdd_set_csr_auth_type(pAdapter, RSNAuthType);
    }
#ifdef FEATURE_WLAN_WAPI
    hddLog(LOG1, "%s: Setting WAPI AUTH Type and Encryption Mode values", __func__);
    if (pAdapter->wapi_info.nWapiMode)
    {
        switch (pAdapter->wapi_info.wapiAuthMode)
        {
            case WAPI_AUTH_MODE_PSK:
            {
                hddLog(LOG1, "%s: WAPI AUTH TYPE: PSK: %d", __func__, pAdapter->wapi_info.wapiAuthMode);
                pRoamProfile->AuthType.numEntries = 1;
                pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_WAPI_WAI_PSK;
                break;
            }
            case WAPI_AUTH_MODE_CERT:
            {
                hddLog(LOG1, "%s: WAPI AUTH TYPE: CERT: %d", __func__, pAdapter->wapi_info.wapiAuthMode);
                pRoamProfile->AuthType.numEntries = 1;
                pRoamProfile->AuthType.authType[0] = eCSR_AUTH_TYPE_WAPI_WAI_CERTIFICATE;
                break;
            }
        } // End of switch
        if ( pAdapter->wapi_info.wapiAuthMode == WAPI_AUTH_MODE_PSK ||
             pAdapter->wapi_info.wapiAuthMode == WAPI_AUTH_MODE_CERT)
        {
            hddLog(LOG1, "%s: WAPI PAIRWISE/GROUP ENCRYPTION: WPI", __func__);
            pRoamProfile->EncryptionType.numEntries = 1;
            pRoamProfile->EncryptionType.encryptionType[0] = eCSR_ENCRYPT_TYPE_WPI;
            pRoamProfile->mcEncryptionType.numEntries = 1;
            pRoamProfile->mcEncryptionType.encryptionType[0] = eCSR_ENCRYPT_TYPE_WPI;
        }
    }
#endif /* FEATURE_WLAN_WAPI */
    /* if previous genIE is not NULL, update AssocIE */
    if (0 != pWextState->genIE.length)
    {
        memset( &pWextState->assocAddIE, 0, sizeof(pWextState->assocAddIE) );
        memcpy( pWextState->assocAddIE.addIEdata, pWextState->genIE.addIEdata,
            pWextState->genIE.length);
        pWextState->assocAddIE.length = pWextState->genIE.length;
        pWextState->roamProfile.pAddIEAssoc = pWextState->assocAddIE.addIEdata;
        pWextState->roamProfile.nAddIEAssocLength = pWextState->assocAddIE.length;

        /* clear previous genIE after use it */
        memset( &pWextState->genIE, 0, sizeof(pWextState->genIE) );
    }

    /* assumes it is not WPS Association by default, except when pAddIEAssoc has WPS IE */
    pWextState->roamProfile.bWPSAssociation = FALSE;

    if (NULL != wlan_hdd_get_wps_ie_ptr(pWextState->roamProfile.pAddIEAssoc,
        pWextState->roamProfile.nAddIEAssocLength))
        pWextState->roamProfile.bWPSAssociation = TRUE;


    // Disable auto BMPS entry by PMC until DHCP is done
    sme_SetDHCPTillPowerActiveFlag(WLAN_HDD_GET_HAL_CTX(pAdapter), TRUE);

    pWextState->roamProfile.csrPersona = pAdapter->device_mode;
    (WLAN_HDD_GET_CTX(pAdapter))->isAmpAllowed = VOS_FALSE;

    if ( eCSR_BSS_TYPE_START_IBSS == pRoamProfile->BSSType )
    {
        hdd_select_cbmode(pAdapter, pHddCtx->cfg_ini->AdHocChannel5G,
                        &ch_width);
        pRoamProfile->vht_channel_width = ch_width;
    }
    /*
     * Change conn_state to connecting before sme_RoamConnect(),
     * because sme_RoamConnect() has a direct path to call
     * hdd_smeRoamCallback(), which will change the conn_state
     * If direct path, conn_state will be accordingly changed
     * to NotConnected or Associated by either
     * hdd_AssociationCompletionHandler() or hdd_DisConnectHandler()
     * in sme_RoamCallback()
     * if sme_RomConnect is to be queued,
     * Connecting state will remain until it is completed.
     *
     * If connection state is not changed,
     * connection state will remain in eConnectionState_NotConnected state.
     * In hdd_AssociationCompletionHandler, "hddDisconInProgress" is set to true
     * if conn state is eConnectionState_NotConnected.
     * If "hddDisconInProgress" is set to true then cfg80211 layer is not
     * informed of connect result indication which is an issue.
     */
    if (WLAN_HDD_INFRA_STATION == pAdapter->device_mode ||
            WLAN_HDD_P2P_CLIENT == pAdapter->device_mode) {
        hdd_connSetConnectionState(pAdapter,
                eConnectionState_Connecting);
    }
    status = sme_RoamConnect( hHal,pAdapter->sessionId,
            &(pWextState->roamProfile), &roamId);

    if ((eHAL_STATUS_SUCCESS != status) &&
            (WLAN_HDD_INFRA_STATION == pAdapter->device_mode ||
             WLAN_HDD_P2P_CLIENT == pAdapter->device_mode)) {
        hddLog(LOGE,
                FL("sme_RoamConnect (session %d) failed with status %d. -> NotConnected"),
                pAdapter->sessionId, status);
        /* change back to NotAssociated */
        hdd_connSetConnectionState(pAdapter,
                eConnectionState_NotConnected);
    }
    pRoamProfile->ChannelInfo.ChannelList = NULL;
    pRoamProfile->ChannelInfo.numOfChannels = 0;

    EXIT();
    return status;
}

/**
 * iw_set_essid() - set essid handler function
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure
 */
int iw_set_essid(struct net_device *dev,
		 struct iw_request_info *info,
		 union iwreq_data *wrqu, char *extra)
{
	int ret;

	vos_ssr_protect(__func__);
	ret = __iw_set_essid(dev, info, wrqu, extra);
	vos_ssr_unprotect(__func__);

	return ret;
}

/**
 * __iw_get_essid() - This function returns the essid to the wpa_supplicant
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure
 */
static int __iw_get_essid(struct net_device *dev,
			  struct iw_request_info *info,
			  struct iw_point *dwrq, char *extra)
{
   hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
   hdd_context_t *hdd_ctx;
   hdd_wext_state_t *wextBuf = WLAN_HDD_GET_WEXT_STATE_PTR(pAdapter);
   hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
   int ret;

   ENTER();

   hdd_ctx = WLAN_HDD_GET_CTX(pAdapter);
   ret = wlan_hdd_validate_context(hdd_ctx);
   if (0 != ret)
       return ret;

   if((pHddStaCtx->conn_info.connState == eConnectionState_Associated &&
     wextBuf->roamProfile.SSIDs.SSIDList->SSID.length > 0) ||
      ((pHddStaCtx->conn_info.connState == eConnectionState_IbssConnected ||
        pHddStaCtx->conn_info.connState == eConnectionState_IbssDisconnected) &&
        wextBuf->roamProfile.SSIDs.SSIDList->SSID.length > 0))
   {
       dwrq->length = pHddStaCtx->conn_info.SSID.SSID.length;
       memcpy(extra, pHddStaCtx->conn_info.SSID.SSID.ssId, dwrq->length);
       dwrq->flags = 1;
   } else {
       memset(extra, 0, dwrq->length);
       dwrq->length = 0;
       dwrq->flags = 0;
   }
   EXIT();
   return 0;
}

/**
 * iw_get_essid() - get essid handler function
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure
 */
int iw_get_essid(struct net_device *dev,
		 struct iw_request_info *info,
		 struct iw_point *wrqu, char *extra)
{
	int ret;

	vos_ssr_protect(__func__);
	ret = __iw_get_essid(dev, info, wrqu, extra);
	vos_ssr_unprotect(__func__);

	return ret;
}

/**
 * __iw_set_auth() - This function sets the auth type received
 *			from the wpa_supplicant.
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure.
 */
static int __iw_set_auth(struct net_device *dev, struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra)
{
   hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
   hdd_context_t *hdd_ctx;
   hdd_wext_state_t *pWextState = WLAN_HDD_GET_WEXT_STATE_PTR(pAdapter);
   hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(pAdapter);
   tCsrRoamProfile *pRoamProfile = &pWextState->roamProfile;
   eCsrEncryptionType mcEncryptionType;
   eCsrEncryptionType ucEncryptionType;
   int ret;

   ENTER();

   hdd_ctx = WLAN_HDD_GET_CTX(pAdapter);
   ret = wlan_hdd_validate_context(hdd_ctx);
   if (0 != ret)
       return ret;

   switch(wrqu->param.flags & IW_AUTH_INDEX)
   {
      case IW_AUTH_WPA_VERSION:

         pWextState->wpaVersion = wrqu->param.value;

         break;

   case IW_AUTH_CIPHER_PAIRWISE:
   {
      if(wrqu->param.value & IW_AUTH_CIPHER_NONE) {
         ucEncryptionType = eCSR_ENCRYPT_TYPE_NONE;
      }
      else if(wrqu->param.value & IW_AUTH_CIPHER_TKIP) {
         ucEncryptionType = eCSR_ENCRYPT_TYPE_TKIP;
      }
      else if(wrqu->param.value & IW_AUTH_CIPHER_CCMP) {
         ucEncryptionType = eCSR_ENCRYPT_TYPE_AES;
      }

     else if(wrqu->param.value & IW_AUTH_CIPHER_WEP40) {

         if( (IW_AUTH_KEY_MGMT_802_1X
                     == (pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X)  )
                 && (eCSR_AUTH_TYPE_OPEN_SYSTEM == pHddStaCtx->conn_info.authType) )
                /*Dynamic WEP key*/
             ucEncryptionType = eCSR_ENCRYPT_TYPE_WEP40;
         else
                /*Static WEP key*/
             ucEncryptionType = eCSR_ENCRYPT_TYPE_WEP40_STATICKEY;
      }
      else if(wrqu->param.value & IW_AUTH_CIPHER_WEP104) {

         if( ( IW_AUTH_KEY_MGMT_802_1X
                     == (pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X) )
                 && (eCSR_AUTH_TYPE_OPEN_SYSTEM == pHddStaCtx->conn_info.authType))
                  /*Dynamic WEP key*/
            ucEncryptionType = eCSR_ENCRYPT_TYPE_WEP104;
         else
                /*Static WEP key*/
            ucEncryptionType = eCSR_ENCRYPT_TYPE_WEP104_STATICKEY;

         }
         else {

               hddLog(LOGW, "%s value %d UNKNOWN IW_AUTH_CIPHER",
                      __func__, wrqu->param.value);
               return -EINVAL;
         }

         pRoamProfile->EncryptionType.numEntries = 1;
         pRoamProfile->EncryptionType.encryptionType[0] = ucEncryptionType;
      }
      break;
      case IW_AUTH_CIPHER_GROUP:
      {
          if(wrqu->param.value & IW_AUTH_CIPHER_NONE) {
            mcEncryptionType = eCSR_ENCRYPT_TYPE_NONE;
      }

      else if(wrqu->param.value & IW_AUTH_CIPHER_TKIP) {
             mcEncryptionType = eCSR_ENCRYPT_TYPE_TKIP;
      }

      else if(wrqu->param.value & IW_AUTH_CIPHER_CCMP) {
              mcEncryptionType = eCSR_ENCRYPT_TYPE_AES;
      }

      else if(wrqu->param.value & IW_AUTH_CIPHER_WEP40) {

         if( ( IW_AUTH_KEY_MGMT_802_1X
                     == (pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X ))
                 && (eCSR_AUTH_TYPE_OPEN_SYSTEM == pHddStaCtx->conn_info.authType))

            mcEncryptionType = eCSR_ENCRYPT_TYPE_WEP40;

         else
               mcEncryptionType = eCSR_ENCRYPT_TYPE_WEP40_STATICKEY;
      }

      else if(wrqu->param.value & IW_AUTH_CIPHER_WEP104)
      {
             /*Dynamic WEP keys won't work with shared keys*/
         if( ( IW_AUTH_KEY_MGMT_802_1X
                     == (pWextState->authKeyMgmt & IW_AUTH_KEY_MGMT_802_1X))
                 && (eCSR_AUTH_TYPE_OPEN_SYSTEM == pHddStaCtx->conn_info.authType))
         {
            mcEncryptionType = eCSR_ENCRYPT_TYPE_WEP104;
         }
         else
         {
            mcEncryptionType = eCSR_ENCRYPT_TYPE_WEP104_STATICKEY;
         }
      }
      else {

          hddLog(LOGW, "%s value %d UNKNOWN IW_AUTH_CIPHER",
                 __func__, wrqu->param.value);
          return -EINVAL;
       }

         pRoamProfile->mcEncryptionType.numEntries = 1;
         pRoamProfile->mcEncryptionType.encryptionType[0] = mcEncryptionType;
      }
      break;

      case IW_AUTH_80211_AUTH_ALG:
      {
           /*Save the auth algo here and set auth type to SME Roam profile
                in the iw_set_ap_address*/
          if( wrqu->param.value & IW_AUTH_ALG_OPEN_SYSTEM)
             pHddStaCtx->conn_info.authType = eCSR_AUTH_TYPE_OPEN_SYSTEM;

          else if(wrqu->param.value & IW_AUTH_ALG_SHARED_KEY)
             pHddStaCtx->conn_info.authType = eCSR_AUTH_TYPE_SHARED_KEY;

          else if(wrqu->param.value & IW_AUTH_ALG_LEAP)
            /*Not supported*/
             pHddStaCtx->conn_info.authType = eCSR_AUTH_TYPE_OPEN_SYSTEM;
          pWextState->roamProfile.AuthType.authType[0] = pHddStaCtx->conn_info.authType;
      }
      break;

      case IW_AUTH_KEY_MGMT:
      {
#ifdef FEATURE_WLAN_ESE
#define IW_AUTH_KEY_MGMT_CCKM       8  /* Should be in linux/wireless.h */
         /*Check for CCKM AKM type */
         if ( wrqu->param.value & IW_AUTH_KEY_MGMT_CCKM) {
            hddLog(VOS_TRACE_LEVEL_INFO,"%s: CCKM AKM Set %d",
                   __func__, wrqu->param.value);
            /* Set the CCKM bit in authKeyMgmt */
            /* Right now, this breaks all ref to authKeyMgmt because our
             * code doesn't realize it is a "bitfield"
             */
            pWextState->authKeyMgmt |= IW_AUTH_KEY_MGMT_CCKM;
            /*Set the key management to 802.1X*/
            //pWextState->authKeyMgmt = IW_AUTH_KEY_MGMT_802_1X;
            pWextState->isESEConnection = eANI_BOOLEAN_TRUE;
            //This is test code. I need to actually KNOW whether this is an RSN Assoc or WPA.
            pWextState->collectedAuthType = eCSR_AUTH_TYPE_CCKM_RSN;
         } else if ( wrqu->param.value & IW_AUTH_KEY_MGMT_PSK) {
            /*Save the key management*/
            pWextState->authKeyMgmt |= IW_AUTH_KEY_MGMT_PSK;
            //pWextState->authKeyMgmt = wrqu->param.value;
            //This is test code. I need to actually KNOW whether this is an RSN Assoc or WPA.
            pWextState->collectedAuthType = eCSR_AUTH_TYPE_RSN;
         } else if (!( wrqu->param.value & IW_AUTH_KEY_MGMT_802_1X)) {
            pWextState->collectedAuthType = eCSR_AUTH_TYPE_NONE; //eCSR_AUTH_TYPE_WPA_NONE
            /*Save the key management anyway*/
            pWextState->authKeyMgmt = wrqu->param.value;
         } else { // It must be IW_AUTH_KEY_MGMT_802_1X
            /*Save the key management*/
            pWextState->authKeyMgmt |= IW_AUTH_KEY_MGMT_802_1X;
            //pWextState->authKeyMgmt = wrqu->param.value;
            //This is test code. I need to actually KNOW whether this is an RSN Assoc or WPA.
            pWextState->collectedAuthType = eCSR_AUTH_TYPE_RSN;
         }
#else
         /*Save the key management*/
         pWextState->authKeyMgmt = wrqu->param.value;
#endif /* FEATURE_WLAN_ESE */
      }
      break;

      case IW_AUTH_TKIP_COUNTERMEASURES:
      {
         if(wrqu->param.value) {
            hddLog(VOS_TRACE_LEVEL_INFO_HIGH,
                   "Counter Measure started %d", wrqu->param.value);
            pWextState->mTKIPCounterMeasures = TKIP_COUNTER_MEASURE_STARTED;
         }
         else {
            hddLog(VOS_TRACE_LEVEL_INFO_HIGH,
                   "Counter Measure stopped=%d", wrqu->param.value);
            pWextState->mTKIPCounterMeasures = TKIP_COUNTER_MEASURE_STOPED;
         }
      }
      break;
      case IW_AUTH_DROP_UNENCRYPTED:
      case IW_AUTH_WPA_ENABLED:
      case IW_AUTH_RX_UNENCRYPTED_EAPOL:
      case IW_AUTH_ROAMING_CONTROL:
      case IW_AUTH_PRIVACY_INVOKED:

      default:

         hddLog(LOGW, "%s called with unsupported auth type %d", __func__,
               wrqu->param.flags & IW_AUTH_INDEX);
      break;
   }

   EXIT();
   return 0;
}

/**
 * iw_set_auth() - set auth callback function
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure.
 */
int iw_set_auth(struct net_device *dev, struct iw_request_info *info,
		union iwreq_data *wrqu, char *extra)
{
	int ret;

	vos_ssr_protect(__func__);
	ret = __iw_set_auth(dev, info, wrqu, extra);
	vos_ssr_unprotect(__func__);

	return ret;
}

/**
 * __iw_get_auth() - This function returns the auth type to the wpa_supplicant.
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure.
 */
static int __iw_get_auth(struct net_device *dev, struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra)
{
    hdd_adapter_t* pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    hdd_context_t *hdd_ctx;
    hdd_wext_state_t *pWextState= WLAN_HDD_GET_WEXT_STATE_PTR(pAdapter);
    tCsrRoamProfile *pRoamProfile = &pWextState->roamProfile;
    int ret;

    ENTER();

    hdd_ctx = WLAN_HDD_GET_CTX(pAdapter);
    ret = wlan_hdd_validate_context(hdd_ctx);
    if (0 != ret)
        return ret;

    switch(pRoamProfile->negotiatedAuthType)
    {
        case eCSR_AUTH_TYPE_WPA_NONE:
            wrqu->param.flags = IW_AUTH_WPA_VERSION;
            wrqu->param.value =  IW_AUTH_WPA_VERSION_DISABLED;
            break;
        case eCSR_AUTH_TYPE_WPA:
            wrqu->param.flags = IW_AUTH_WPA_VERSION;
            wrqu->param.value = IW_AUTH_WPA_VERSION_WPA;
            break;
#ifdef WLAN_FEATURE_VOWIFI_11R
        case eCSR_AUTH_TYPE_FT_RSN:
#endif
        case eCSR_AUTH_TYPE_RSN:
            wrqu->param.flags = IW_AUTH_WPA_VERSION;
            wrqu->param.value =  IW_AUTH_WPA_VERSION_WPA2;
            break;
         case eCSR_AUTH_TYPE_OPEN_SYSTEM:
             wrqu->param.value = IW_AUTH_ALG_OPEN_SYSTEM;
             break;
         case eCSR_AUTH_TYPE_SHARED_KEY:
             wrqu->param.value =  IW_AUTH_ALG_SHARED_KEY;
             break;
         case eCSR_AUTH_TYPE_UNKNOWN:
             hddLog(LOG1,"%s called with unknown auth type", __func__);
             wrqu->param.value =  IW_AUTH_ALG_OPEN_SYSTEM;
             break;
         case eCSR_AUTH_TYPE_AUTOSWITCH:
             wrqu->param.value =  IW_AUTH_ALG_OPEN_SYSTEM;
             break;
         case eCSR_AUTH_TYPE_WPA_PSK:
             hddLog(LOG1,"%s called with WPA PSK auth type", __func__);
             wrqu->param.value = IW_AUTH_ALG_OPEN_SYSTEM;
             return -EIO;
#ifdef WLAN_FEATURE_VOWIFI_11R
         case eCSR_AUTH_TYPE_FT_RSN_PSK:
#endif
         case eCSR_AUTH_TYPE_RSN_PSK:
#ifdef WLAN_FEATURE_11W
         case eCSR_AUTH_TYPE_RSN_PSK_SHA256:
         case eCSR_AUTH_TYPE_RSN_8021X_SHA256:
#endif
             hddLog(LOG1,"%s called with RSN PSK auth type", __func__);
             wrqu->param.value = IW_AUTH_ALG_OPEN_SYSTEM;
             return -EIO;
         default:
             hddLog(LOGE,"%s called with unknown auth type", __func__);
             wrqu->param.value = IW_AUTH_ALG_OPEN_SYSTEM;
             return -EIO;
    }
    if(((wrqu->param.flags & IW_AUTH_INDEX) == IW_AUTH_CIPHER_PAIRWISE))
    {
        switch(pRoamProfile->negotiatedUCEncryptionType)
        {
            case eCSR_ENCRYPT_TYPE_NONE:
                wrqu->param.value = IW_AUTH_CIPHER_NONE;
                break;
            case eCSR_ENCRYPT_TYPE_WEP40:
            case eCSR_ENCRYPT_TYPE_WEP40_STATICKEY:
                wrqu->param.value = IW_AUTH_CIPHER_WEP40;
                break;
            case eCSR_ENCRYPT_TYPE_TKIP:
                wrqu->param.value = IW_AUTH_CIPHER_TKIP;
                break;
            case eCSR_ENCRYPT_TYPE_WEP104:
            case eCSR_ENCRYPT_TYPE_WEP104_STATICKEY:
                wrqu->param.value = IW_AUTH_CIPHER_WEP104;
                break;
            case eCSR_ENCRYPT_TYPE_AES:
                wrqu->param.value = IW_AUTH_CIPHER_CCMP;
                break;
            default:
                hddLog(LOG1, "%s called with unknown auth type %d ",
                       __func__, pRoamProfile->negotiatedUCEncryptionType);
                return -EIO;
        }
   }

    if(((wrqu->param.flags & IW_AUTH_INDEX) == IW_AUTH_CIPHER_GROUP))
    {
        switch(pRoamProfile->negotiatedMCEncryptionType)
        {
        case eCSR_ENCRYPT_TYPE_NONE:
            wrqu->param.value = IW_AUTH_CIPHER_NONE;
            break;
        case eCSR_ENCRYPT_TYPE_WEP40:
        case eCSR_ENCRYPT_TYPE_WEP40_STATICKEY:
            wrqu->param.value = IW_AUTH_CIPHER_WEP40;
            break;
        case eCSR_ENCRYPT_TYPE_TKIP:
            wrqu->param.value = IW_AUTH_CIPHER_TKIP;
            break;
         case eCSR_ENCRYPT_TYPE_WEP104:
         case eCSR_ENCRYPT_TYPE_WEP104_STATICKEY:
             wrqu->param.value = IW_AUTH_CIPHER_WEP104;
             break;
         case eCSR_ENCRYPT_TYPE_AES:
             wrqu->param.value = IW_AUTH_CIPHER_CCMP;
             break;
         default:
             hddLog(LOG1, "%s called with unknown auth type %d ",
                    __func__, pRoamProfile->negotiatedMCEncryptionType);
            return -EIO;
       }
   }

    hddLog(LOG1, "%s called with auth type %d",
           __func__, pRoamProfile->AuthType.authType[0]);
    EXIT();
    return 0;
}

/**
 * iw_get_auth() - get auth callback function
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure.
 */
int iw_get_auth(struct net_device *dev, struct iw_request_info *info,
		union iwreq_data *wrqu, char *extra)
{
	int ret;

	vos_ssr_protect(__func__);
	ret = __iw_get_auth(dev, info, wrqu, extra);
	vos_ssr_unprotect(__func__);

	return ret;
}

/**
 * __iw_set_ap_address() - This function calls the sme_RoamConnect function
 *			to associate to the AP with the specified
 *			BSSID received from the wpa_supplicant.
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure.
 */
static int __iw_set_ap_address(struct net_device *dev,
        struct iw_request_info *info,
        union iwreq_data *wrqu, char *extra)
{

    hdd_adapter_t *adapter;
    hdd_context_t *hdd_ctx;
    hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(WLAN_HDD_GET_PRIV_PTR(dev));
    v_U8_t  *pMacAddress=NULL;
    int ret;

    ENTER();

    adapter = WLAN_HDD_GET_PRIV_PTR(dev);

    hdd_ctx = WLAN_HDD_GET_CTX(adapter);
    ret = wlan_hdd_validate_context(hdd_ctx);
    if (0 != ret)
        return ret;

    pMacAddress = (v_U8_t*) wrqu->ap_addr.sa_data;
    VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO, "%s "MAC_ADDRESS_STR,
              __func__, MAC_ADDR_ARRAY(pMacAddress));
    vos_mem_copy( pHddStaCtx->conn_info.bssId, pMacAddress, sizeof( tCsrBssid ));
    EXIT();

    return 0;
}

/**
 * iw_set_ap_address() - set ap addresses callback function
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure.
 */
int iw_set_ap_address(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	int ret;

	vos_ssr_protect(__func__);
	ret = __iw_set_ap_address(dev, info, wrqu, extra);
	vos_ssr_unprotect(__func__);

	return ret;
}

/**
 * __iw_get_ap_address() - This function returns the BSSID to the wpa_supplicant
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure.
 */
static int __iw_get_ap_address(struct net_device *dev,
                             struct iw_request_info *info,
                             union iwreq_data *wrqu, char *extra)
{
    hdd_adapter_t *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
    hdd_context_t *hdd_ctx;
    hdd_station_ctx_t *pHddStaCtx = WLAN_HDD_GET_STATION_CTX_PTR(adapter);
    int ret;

    ENTER();

    hdd_ctx = WLAN_HDD_GET_CTX(adapter);
    ret = wlan_hdd_validate_context(hdd_ctx);
    if (0 != ret)
        return ret;

    if ((pHddStaCtx->conn_info.connState == eConnectionState_Associated) ||
        (eConnectionState_IbssConnected == pHddStaCtx->conn_info.connState))
    {
        memcpy(wrqu->ap_addr.sa_data,pHddStaCtx->conn_info.bssId,ETH_ALEN);
    }
    else
    {
        memset(wrqu->ap_addr.sa_data,0,sizeof(wrqu->ap_addr.sa_data));
    }
    EXIT();
    return 0;
}

/**
 * iw_get_ap_address() - get ap addresses callback function
 * @dev: Pointer to the net device.
 * @info: Pointer to the iw_request_info.
 * @wrqu: Pointer to the iwreq_data.
 * @extra: Pointer to the data.
 *
 * Return: 0 for success, error number on failure.
 */
int iw_get_ap_address(struct net_device *dev, struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	int ret;

	vos_ssr_protect(__func__);
	ret = __iw_get_ap_address(dev, info, wrqu, extra);
	vos_ssr_unprotect(__func__);

	return ret;
}

hdd_adapter_t *hdd_get_sta_connection_in_progress(hdd_context_t *hdd_ctx)
{
	hdd_adapter_list_node_t *adapter_node = NULL, *next = NULL;
	hdd_adapter_t *adapter = NULL;
	VOS_STATUS status;
	hdd_station_ctx_t *hdd_sta_ctx;

	if (!hdd_ctx) {
		hddLog(LOGE, FL("HDD context is NULL"));
		return NULL;
	}

	status = hdd_get_front_adapter(hdd_ctx, &adapter_node);
	while (NULL != adapter_node && VOS_STATUS_SUCCESS == status) {
		adapter = adapter_node->pAdapter;
		if (!adapter)
			goto end;

		hdd_sta_ctx = WLAN_HDD_GET_STATION_CTX_PTR(adapter);
		if ((WLAN_HDD_INFRA_STATION == adapter->device_mode) ||
		    (WLAN_HDD_P2P_CLIENT == adapter->device_mode) ||
		    (WLAN_HDD_P2P_DEVICE == adapter->device_mode)) {
			if (eConnectionState_Connecting ==
			    hdd_sta_ctx->conn_info.connState) {
				hddLog(LOG1, FL("session_id %d: Connection is in progress"),
				       adapter->sessionId);
				return adapter;
			} else if ((eConnectionState_Associated ==
				   hdd_sta_ctx->conn_info.connState) &&
				   sme_is_sta_key_exchange_in_progress(
				   hdd_ctx->hHal, adapter->sessionId)) {
				hddLog(LOG1, FL("session_id %d: Key exchange is in progress"),
				       adapter->sessionId);
				return adapter;
			}
		}
end:
		status = hdd_get_next_adapter(hdd_ctx, adapter_node, &next);
		adapter_node = next;
	}
	return NULL;
}
