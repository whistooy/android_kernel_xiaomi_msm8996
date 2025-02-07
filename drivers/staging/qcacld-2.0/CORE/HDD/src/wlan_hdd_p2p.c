/*
 * Copyright (c) 2012-2019 The Linux Foundation. All rights reserved.
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

  \file  wlan_hdd_p2p.c

  \brief WLAN Host Device Driver implementation for P2P commands interface

  ========================================================================*/

#include <wlan_hdd_includes.h>
#include <wlan_hdd_hostapd.h>
#include <net/cfg80211.h>
#include "sme_Api.h"
#include "sme_QosApi.h"
#include "wlan_hdd_p2p.h"
#include "sapApi.h"
#include "wlan_hdd_main.h"
#include "vos_trace.h"
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <net/ieee80211_radiotap.h>
#include "wlan_hdd_tdls.h"
#include "wlan_hdd_trace.h"
#include "vos_types.h"
#include "vos_trace.h"
#include "vos_sched.h"
#include "wlan_hdd_request_manager.h"

//Ms to Micro Sec
#define MS_TO_MUS(x)   ((x)*1000)

#ifdef WLAN_DEBUG
static tANI_U8* hdd_getActionString(tANI_U16 MsgType)
{
    switch (MsgType)
    {
       CASE_RETURN_STRING(SIR_MAC_ACTION_SPECTRUM_MGMT);
       CASE_RETURN_STRING(SIR_MAC_ACTION_QOS_MGMT);
       CASE_RETURN_STRING(SIR_MAC_ACTION_DLP);
       CASE_RETURN_STRING(SIR_MAC_ACTION_BLKACK);
       CASE_RETURN_STRING(SIR_MAC_ACTION_PUBLIC_USAGE);
       CASE_RETURN_STRING(SIR_MAC_ACTION_RRM);
       CASE_RETURN_STRING(SIR_MAC_ACTION_FAST_BSS_TRNST);
       CASE_RETURN_STRING(SIR_MAC_ACTION_HT);
       CASE_RETURN_STRING(SIR_MAC_ACTION_SA_QUERY);
       CASE_RETURN_STRING(SIR_MAC_ACTION_PROT_DUAL_PUB);
       CASE_RETURN_STRING(SIR_MAC_ACTION_WNM);
       CASE_RETURN_STRING(SIR_MAC_ACTION_UNPROT_WNM);
       CASE_RETURN_STRING(SIR_MAC_ACTION_TDLS);
       CASE_RETURN_STRING(SIR_MAC_ACITON_MESH);
       CASE_RETURN_STRING(SIR_MAC_ACTION_MHF);
       CASE_RETURN_STRING(SIR_MAC_SELF_PROTECTED);
       CASE_RETURN_STRING(SIR_MAC_ACTION_WME);
       CASE_RETURN_STRING(SIR_MAC_ACTION_VHT);
       default:
           return ("UNKNOWN");
     }
}
#endif

#ifdef WLAN_FEATURE_P2P_DEBUG
#define MAX_P2P_ACTION_FRAME_TYPE 9
const char *p2p_action_frame_type[]={"GO Negotiation Request",
                                     "GO Negotiation Response",
                                     "GO Negotiation Confirmation",
                                     "P2P Invitation Request",
                                     "P2P Invitation Response",
                                     "Device Discoverability Request",
                                     "Device Discoverability Response",
                                     "Provision Discovery Request",
                                     "Provision Discovery Response"};

/* We no need to protect this variable since
 * there is no chance of race to condition
 * and also not make any complicating the code
 * just for debugging log
 */
tP2PConnectionStatus globalP2PConnectionStatus = P2P_NOT_ACTIVE;

#endif
#define MAX_TDLS_ACTION_FRAME_TYPE 11
const char *tdls_action_frame_type[] = {"TDLS Setup Request",
                                        "TDLS Setup Response",
                                        "TDLS Setup Confirm",
                                        "TDLS Teardown",
                                        "TDLS Peer Traffic Indication",
                                        "TDLS Channel Switch Request",
                                        "TDLS Channel Switch Response",
                                        "TDLS Peer PSM Request",
                                        "TDLS Peer PSM Response",
                                        "TDLS Peer Traffic Response",
                                        "TDLS Discovery Request" };

extern struct net_device_ops net_ops_struct;

static bool wlan_hdd_is_type_p2p_action( const u8 *buf, uint32_t len)
{
    const u8 *ouiPtr;

    if (len < WLAN_HDD_PUBLIC_ACTION_FRAME_SUB_TYPE_OFFSET + 1)
        return FALSE;

    if ( buf[WLAN_HDD_PUBLIC_ACTION_FRAME_CATEGORY_OFFSET] !=
               WLAN_HDD_PUBLIC_ACTION_FRAME ) {
        return FALSE;
    }

    if ( buf[WLAN_HDD_PUBLIC_ACTION_FRAME_ACTION_OFFSET] !=
               WLAN_HDD_VENDOR_SPECIFIC_ACTION ) {
        return FALSE;
    }

    ouiPtr = &buf[WLAN_HDD_PUBLIC_ACTION_FRAME_OUI_OFFSET];

    if ( WPA_GET_BE24(ouiPtr) != WLAN_HDD_WFA_OUI ) {
        return FALSE;
    }

    if ( buf[WLAN_HDD_PUBLIC_ACTION_FRAME_OUI_TYPE_OFFSET] !=
               WLAN_HDD_WFA_P2P_OUI_TYPE ) {
        return FALSE;
    }

    return TRUE;
}

static bool hdd_p2p_is_action_type_rsp( const u8 *buf, uint32_t len )
{
    tActionFrmType actionFrmType;

    if ( wlan_hdd_is_type_p2p_action(buf, len) )
    {
        actionFrmType = buf[WLAN_HDD_PUBLIC_ACTION_FRAME_SUB_TYPE_OFFSET];
        if ( actionFrmType != WLAN_HDD_INVITATION_REQ &&
            actionFrmType != WLAN_HDD_GO_NEG_REQ &&
            actionFrmType != WLAN_HDD_DEV_DIS_REQ &&
            actionFrmType != WLAN_HDD_PROV_DIS_REQ )
            return TRUE;
    }

    return FALSE;
}

struct random_mac_priv {
	bool set_random_addr;
};

/**
 * hdd_random_mac_callback() - Callback invoked from wmi layer
 * @set_random_addr: Status of random mac filter set operation
 * @context: Context passed while registring callback
 *
 * This function is invoked from wmi layer to give the status of
 * random mac filter set operation by firmware.
 *
 * Return: None
 */
static void hdd_random_mac_callback(bool set_random_addr, void *context)
{
	struct hdd_request *request;
	struct random_mac_priv *priv;

	request = hdd_request_get(context);
	if (!request) {
		hddLog(LOGE,FL("invalid request"));
		return;
	}

	priv = hdd_request_priv(request);
	priv->set_random_addr = set_random_addr;

	hdd_request_complete(request);
	hdd_request_put(request);
}

/**
 * hdd_set_random_mac() - Invoke sme api to set random mac filter
 * @adapter: Pointer to adapter
 * @random_mac_addr: Mac addr filter to be set
 *
 * Return: If set is successful return true else return false
 */
static bool hdd_set_random_mac(hdd_adapter_t *adapter, uint8_t *random_mac_addr)
{
	hdd_context_t *hdd_ctx;
	eHalStatus sme_status;
	unsigned long rc;
	void *cookie;
	bool status = false;
	struct hdd_request     *request;
	struct random_mac_priv *priv;
	static const struct hdd_request_params params = {
		.priv_size = sizeof(*priv),
		.timeout_ms = WLAN_WAIT_TIME_SET_RND,
	};

	ENTER();
	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	if (wlan_hdd_validate_context(hdd_ctx)) {
		hddLog(LOGE,FL("Invalid hdd ctx"));
		return false;
	}

	request = hdd_request_alloc(&params);
	if (!request) {
		hddLog(LOGE, FL("Request allocation failure"));
		return false;
	}

	cookie = hdd_request_cookie(request);

	sme_status = sme_set_random_mac(hdd_ctx->hHal, hdd_random_mac_callback,
				     adapter->sessionId, random_mac_addr,
				     cookie);

	if (sme_status != eHAL_STATUS_SUCCESS) {
		hddLog(LOGE,FL("Unable to set random mac"));
	} else {
		rc = hdd_request_wait_for_response(request);
		if (rc) {
			hddLog(LOGE, FL("SME timed out while setting random mac"));
		} else {
			priv = hdd_request_priv(request);
			status = priv->set_random_addr;
		}
	}

	hdd_request_put(request);
	EXIT();

	return status;
}

/**
 * hdd_clear_random_mac() - Invoke sme api to clear random mac filter
 * @adapter: Pointer to adapter
 * @random_mac_addr: Mac addr filter to be cleared
 *
 * Return: If clear is successful return true else return false
 */
static bool hdd_clear_random_mac(hdd_adapter_t *adapter,
				 uint8_t *random_mac_addr)
{
	hdd_context_t *hdd_ctx;
	eHalStatus status;

	ENTER();
	hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	if (wlan_hdd_validate_context(hdd_ctx)) {
		hddLog(LOGE,FL("Invalid hdd ctx"));
		return false;
	}

	status = sme_clear_random_mac(hdd_ctx->hHal, adapter->sessionId,
				      random_mac_addr);

	if (status != eHAL_STATUS_SUCCESS) {
		hddLog(LOGE,FL("Unable to clear random mac"));
		return false;
	}

	EXIT();
	return true;
}

bool hdd_check_random_mac(hdd_adapter_t *adapter, uint8_t *random_mac_addr)
{
	uint32_t i = 0;

	adf_os_spin_lock(&adapter->random_mac_lock);
	for (i = 0; i < MAX_RANDOM_MAC_ADDRS; i++) {
		if ((adapter->random_mac[i].in_use) &&
		    (!memcmp(adapter->random_mac[i].addr, random_mac_addr,
			     VOS_MAC_ADDR_SIZE))) {
			adf_os_spin_unlock(&adapter->random_mac_lock);
			return true;
		}
	}
	adf_os_spin_unlock(&adapter->random_mac_lock);
	return false;
}

/**
 * find_action_frame_cookie() - Checks for action cookie in cookie list
 * @cookie_list: List of cookies
 * @cookie: Cookie to be searched
 *
 * Return: If search is successful return pointer to action_frame_cookie
 * object in which cookie item is encapsulated.
 */
static struct action_frame_cookie * find_action_frame_cookie(
						struct list_head *cookie_list,
						uint64_t cookie)
{
	struct action_frame_cookie *action_cookie = NULL;
	struct list_head *temp = NULL;

	list_for_each(temp, cookie_list) {
		action_cookie = list_entry(temp, struct action_frame_cookie,
					   cookie_node);
		if (action_cookie->cookie == cookie)
			return action_cookie;
	}

	return NULL;
}

/**
 * allocate_action_frame_cookie() - Allocate and add action cookie to given list
 * @cookie_list: List of cookies
 * @cookie: Cookie to be added
 *
 * Return: If allocation and addition is successful return pointer to
 * action_frame_cookie object in which cookie item is encapsulated.
 */
static struct action_frame_cookie * allocate_action_frame_cookie(
						struct list_head *cookie_list,
						uint64_t cookie)
{
	struct action_frame_cookie *action_cookie = NULL;

	action_cookie = vos_mem_malloc(sizeof(*action_cookie));
	if(!action_cookie)
		return NULL;

	action_cookie->cookie = cookie;
	list_add(&action_cookie->cookie_node, cookie_list);

	return action_cookie;
}

/**
 * delete_action_frame_cookie() - Delete the cookie from given list
 * @cookie_list: List of cookies
 * @cookie: Cookie to be deleted
 *
 * This function deletes the cookie item from given list and corresponding
 * object in which it is encapsulated.
 *
 * Return: None
 */
static void delete_action_frame_cookie(
				struct action_frame_cookie *action_cookie)
{
	list_del(&action_cookie->cookie_node);
	vos_mem_free(action_cookie);
}

/**
 * append_action_frame_cookie() - Append action cookie to given list
 * @cookie_list: List of cookies
 * @cookie: Cookie to be append
 *
 * This is a wrapper function which invokes allocate_action_frame_cookie
 * if the cookie to be added is not duplicate
 *
 * Return: 0 - for successfull case
 *         -EALREADY - if cookie is duplicate
 *         -ENOMEM - if allocation is failed
 */
static int32_t append_action_frame_cookie(struct list_head *cookie_list,
					  uint64_t cookie)
{
	struct action_frame_cookie *action_cookie = NULL;

	/*
	 * There should be no mac entry with empty cookie list,
	 * check and ignore if duplicate
	 */
	action_cookie = find_action_frame_cookie(cookie_list, cookie);
	if (action_cookie)
		/* random mac address is already programmed */
		return -EALREADY;

	/* insert new cookie in cookie list */
	action_cookie = allocate_action_frame_cookie(cookie_list, cookie);
	if (!action_cookie)
		return -ENOMEM;

	return 0;
}

/**
 * hdd_set_action_frame_random_mac() - Store action frame cookie
 * @adapter: Pointer to adapter
 * @random_mac_addr: Mac addr for cookie
 * @cookie: Cookie to be stored
 *
 * This function is used to create cookie list and append the cookies
 * to same for corresponding random mac addr. If this cookie is the first
 * item in the list then random mac filter is set.
 *
 * Return: 0 - for success else negative value
 */
static int32_t hdd_set_action_frame_random_mac(hdd_adapter_t *adapter,
					       uint8_t *random_mac_addr,
					       uint64_t cookie)
{
	uint32_t i = 0;
	uint32_t in_use_cnt = 0;
	struct action_frame_cookie *action_cookie = NULL;
	int32_t append_ret = 0;

	if (!cookie) {
		hddLog(LOGE, FL("Invalid cookie"));
		return -EINVAL;
	}

	hddLog(LOG1, FL("mac_addr: " MAC_ADDRESS_STR " && cookie = %llu"),
			MAC_ADDR_ARRAY(random_mac_addr), cookie);

	adf_os_spin_lock(&adapter->random_mac_lock);
	for (i = 0; i < MAX_RANDOM_MAC_ADDRS; i++) {
		if (adapter->random_mac[i].in_use) {
			in_use_cnt++;
			if (!memcmp(adapter->random_mac[i].addr,
				random_mac_addr, VOS_MAC_ADDR_SIZE))
				break;
		}
	}

	if (i != MAX_RANDOM_MAC_ADDRS) {
		append_ret = append_action_frame_cookie(
					&adapter->random_mac[i].cookie_list,
					cookie);
		adf_os_spin_unlock(&adapter->random_mac_lock);

		if(append_ret == -ENOMEM) {
			hddLog(LOGE, FL("No Sufficient memory for cookie"));
			return append_ret;
		}

		return 0;
	}

	/* get the first unused buf and store new random mac */
	for (i = 0; i < MAX_RANDOM_MAC_ADDRS; i++) {
		if (!adapter->random_mac[i].in_use)
			break;
	}

	if ((in_use_cnt == MAX_RANDOM_MAC_ADDRS)
		|| (i == MAX_RANDOM_MAC_ADDRS)) {
		adf_os_spin_unlock(&adapter->random_mac_lock);
		hddLog(LOGE, FL("Reached the limit of Max random addresses"));
		return -EBUSY;
	}

	INIT_LIST_HEAD(&adapter->random_mac[i].cookie_list);
	action_cookie = allocate_action_frame_cookie(&adapter->random_mac[i].cookie_list,
					cookie);
	if(!action_cookie) {
		adf_os_spin_unlock(&adapter->random_mac_lock);
		hddLog(LOGE, FL("No Sufficient memory for cookie"));
		return -ENOMEM;
	}
	vos_mem_copy(adapter->random_mac[i].addr, random_mac_addr,
		     VOS_MAC_ADDR_SIZE);
	adapter->random_mac[i].in_use = true;
	adf_os_spin_unlock(&adapter->random_mac_lock);
	/* Program random mac_addr */
	if (!hdd_set_random_mac(adapter, adapter->random_mac[i].addr)) {
		adf_os_spin_lock(&adapter->random_mac_lock);
		/* clear the cookie */
		delete_action_frame_cookie(action_cookie);
		adapter->random_mac[i].in_use = false;
		adf_os_spin_unlock(&adapter->random_mac_lock);
		hddLog(LOGE, FL("random mac filter set failed for: "
				MAC_ADDRESS_STR),
				MAC_ADDR_ARRAY(adapter->random_mac[i].addr));
		return -EFAULT;
	}

	return 0;
}

/**
 * hdd_reset_action_frame_random_mac() - Delete action frame cookie with
 * given random mac addr
 * @adapter: Pointer to adapter
 * @random_mac_addr: Mac addr for cookie
 * @cookie: Cookie to be deleted
 *
 * This function is used to delete the cookie from the cookie list corresponding
 * to given random mac addr.If cookie list is empty after deleting,
 * it will clear random mac filter.
 *
 * Return: 0 - for success else negative value
 */
static int32_t hdd_reset_action_frame_random_mac(hdd_adapter_t *adapter,
						 uint8_t *random_mac_addr,
						 uint64_t cookie)
{
	uint32_t i = 0;
	struct action_frame_cookie *action_cookie = NULL;

	if (!cookie) {
		hddLog(LOGE, FL("Invalid cookie"));
		return -EINVAL;
	}

	hddLog(LOG1, FL("mac_addr: " MAC_ADDRESS_STR " && cookie = %llu"),
			MAC_ADDR_ARRAY(random_mac_addr), cookie);

	adf_os_spin_lock(&adapter->random_mac_lock);
	for (i = 0; i < MAX_RANDOM_MAC_ADDRS; i++) {
		if ((adapter->random_mac[i].in_use) &&
		    (!memcmp(adapter->random_mac[i].addr,
			     random_mac_addr, VOS_MAC_ADDR_SIZE)))
			break;
	}

	if (i == MAX_RANDOM_MAC_ADDRS) {
		adf_os_spin_unlock(&adapter->random_mac_lock);
		hddLog(LOGE, FL("trying to delete cookie of random mac-addr"
				" for which entry is not present"));
		return -EINVAL;
	}

	action_cookie = find_action_frame_cookie(&adapter->random_mac[i].cookie_list,
				    cookie);

	if (!action_cookie) {
		adf_os_spin_unlock(&adapter->random_mac_lock);
		hddLog(LOG1, FL("No cookie matches"));
		return 0;
	}

	delete_action_frame_cookie(action_cookie);
	if (list_empty(&adapter->random_mac[i].cookie_list)) {
		adapter->random_mac[i].in_use = false;
		adf_os_spin_unlock(&adapter->random_mac_lock);
		hdd_clear_random_mac(adapter, random_mac_addr);
		hddLog(LOG1, FL("Deleted random mac_addr:"
				MAC_ADDRESS_STR),
				MAC_ADDR_ARRAY(random_mac_addr));
		return 0;
	}

	adf_os_spin_unlock(&adapter->random_mac_lock);
	return 0;
}

/**
 * hdd_delete_action_frame_cookie() - Delete action frame cookie
 * @adapter: Pointer to adapter
 * @cookie: Cookie to be deleted
 *
 * This function parses the cookie list of each random mac addr until the
 * specified cookie is found and then deletes it. If cookie list is empty
 * after deleting, it will clear random mac filter.
 *
 * Return: 0 - for success else negative value
 */
static int32_t hdd_delete_action_frame_cookie(hdd_adapter_t *adapter,
					      uint64_t cookie)
{
	uint32_t i = 0;
	struct action_frame_cookie *action_cookie = NULL;

	hddLog(LOG1, FL("Delete cookie = %llu"), cookie);

        adf_os_spin_lock(&adapter->random_mac_lock);
        for (i = 0; i < MAX_RANDOM_MAC_ADDRS; i++) {
		if (!adapter->random_mac[i].in_use)
			continue;

		action_cookie = find_action_frame_cookie(&adapter->random_mac[i].cookie_list,
					    cookie);

		if (!action_cookie)
			continue;

		delete_action_frame_cookie(action_cookie);

		if (list_empty(&adapter->random_mac[i].cookie_list)) {
			adapter->random_mac[i].in_use = false;
			adf_os_spin_unlock(&adapter->random_mac_lock);
			hdd_clear_random_mac(adapter,
					     adapter->random_mac[i].addr);
			hddLog(LOG1, FL("Deleted random addr "MAC_ADDRESS_STR),
				MAC_ADDR_ARRAY(adapter->random_mac[i].addr));
			return 0;
		}
		adf_os_spin_unlock(&adapter->random_mac_lock);
		return 0;
	}

	adf_os_spin_unlock(&adapter->random_mac_lock);
	hddLog(LOG1, FL("Invalid cookie"));
	return -EINVAL;
}

/**
 * hdd_delete_all_action_frame_cookies() - Delete all action frame cookies
 * @adapter: Pointer to adapter
 *
 * This function deletes all the cookie lists of each random mac addr and clears
 * the corresponding random mac filters.
 *
 * Return: 0 - for success else negative value
 */
static void hdd_delete_all_action_frame_cookies(hdd_adapter_t *adapter)
{
	uint32_t i = 0;
	struct action_frame_cookie *action_cookie = NULL;
	struct list_head *n;
	struct list_head *temp;

	adf_os_spin_lock(&adapter->random_mac_lock);

	for (i = 0; i < MAX_RANDOM_MAC_ADDRS; i++) {

		if (!adapter->random_mac[i].in_use)
			continue;

		/* empty the list and clear random addr */
		list_for_each_safe(temp, n,
				   &adapter->random_mac[i].cookie_list) {
			action_cookie = list_entry(temp,
						   struct action_frame_cookie,
						   cookie_node);
			list_del(temp);
			vos_mem_free(action_cookie);
		}

		adapter->random_mac[i].in_use = false;
		adf_os_spin_unlock(&adapter->random_mac_lock);
		hdd_clear_random_mac(adapter, adapter->random_mac[i].addr);
		hddLog(LOG1, FL("Deleted random addr " MAC_ADDRESS_STR),
				MAC_ADDR_ARRAY(adapter->random_mac[i].addr));
		adf_os_spin_lock(&adapter->random_mac_lock);
	}

	adf_os_spin_unlock(&adapter->random_mac_lock);
}

static eHalStatus
wlan_hdd_remain_on_channel_callback(tHalHandle hHal, void* pCtx,
                                    eHalStatus status)
{
    hdd_adapter_t *pAdapter = (hdd_adapter_t*) pCtx;
    hdd_cfg80211_state_t *cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );
    hdd_remain_on_chan_ctx_t *pRemainChanCtx;
    hdd_context_t *hdd_ctx = WLAN_HDD_GET_CTX(pAdapter);
    int ret_code;

    ret_code = wlan_hdd_validate_context(hdd_ctx);
    if (0 != ret_code) {
        /* If ssr is inprogress, do not return, resource release is necessary */
        if (!(-EAGAIN == ret_code && hdd_ctx->isLogpInProgress)) {
            return eHAL_STATUS_FAILURE;
        }
    }

    mutex_lock(&cfgState->remain_on_chan_ctx_lock);
    pRemainChanCtx = cfgState->remain_on_chan_ctx;

    if( pRemainChanCtx == NULL )
    {
       mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
       hddLog( LOGW,
          "%s: No Rem on channel pending for which Rsp is received", __func__);
       return eHAL_STATUS_SUCCESS;
    }

    hddLog( LOG1, "Received remain on channel rsp");
    if (!VOS_IS_STATUS_SUCCESS(vos_timer_stop(
                    &pRemainChanCtx->hdd_remain_on_chan_timer)))
        hddLog( LOGE, FL("Failed to stop hdd_remain_on_chan_timer"));
    if (!VOS_IS_STATUS_SUCCESS(vos_timer_destroy(
                    &pRemainChanCtx->hdd_remain_on_chan_timer)))
        hddLog( LOGE, FL("Failed to destroy hdd_remain_on_chan_timer"));
    cfgState->remain_on_chan_ctx = NULL;
    /*
     * Resetting the roc in progress early ensures that the subsequent
     * roc requests are immediately processed without being queued
     */
    pAdapter->is_roc_inprogress = false;
    vos_runtime_pm_allow_suspend(hdd_ctx->runtime_context.roc);
    /*
     * If the allow suspend is done later, the scheduled roc wil prevent
     * the system from going into suspend and immediately this logic
     * will allow the system to go to suspend breaking the exising logic.
     * Basically, the system must not go into suspend while roc is in progress.
     */
    hdd_allow_suspend(WIFI_POWER_EVENT_WAKELOCK_ROC);

    if( REMAIN_ON_CHANNEL_REQUEST == pRemainChanCtx->rem_on_chan_request)
    {
        if( cfgState->buf )
        {
           hddLog( LOG1,
                   "%s: We need to receive yet an ack from one of tx packet",
                   __func__);
        }
        cfg80211_remain_on_channel_expired(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                              pRemainChanCtx->dev->ieee80211_ptr,
#else
                              pRemainChanCtx->dev,
#endif
                              pRemainChanCtx->cookie,
                              &pRemainChanCtx->chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                              pRemainChanCtx->chan_type,
#endif
                              GFP_KERNEL);
        pAdapter->lastRocTs = vos_timer_get_system_time();
    }
    mutex_unlock(&cfgState->remain_on_chan_ctx_lock);

    /* Schedule any pending RoC: Any new roc request during this time
     * would have got queued in 'wlan_hdd_request_remain_on_channel'
     * since the queue is not empty. So, the roc at the head of the
     * queue will only get the priority. Scheduling the work queue
     * after sending any cancel remain on channel event will also
     * ensure that the cancel roc is sent without any delays.
     */
     /* If ssr is inprogress, do not schedule next roc req */
     if (!hdd_ctx->isLogpInProgress)
        queue_delayed_work(system_freezable_power_efficient_wq,
                           &hdd_ctx->rocReqWork, 0);

    if ( ( WLAN_HDD_INFRA_STATION == pAdapter->device_mode ) ||
         ( WLAN_HDD_P2P_CLIENT == pAdapter->device_mode ) ||
         ( WLAN_HDD_P2P_DEVICE == pAdapter->device_mode )
       )
    {
        tANI_U8 sessionId = pAdapter->sessionId;
        mutex_lock(&cfgState->remain_on_chan_ctx_lock);
        if( REMAIN_ON_CHANNEL_REQUEST == pRemainChanCtx->rem_on_chan_request)
        {
            mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
            sme_DeregisterMgmtFrame(
                       hHal, sessionId,
                      (SIR_MAC_MGMT_FRAME << 2) | ( SIR_MAC_MGMT_PROBE_REQ << 4),
                       NULL, 0 );
        }
        else
           mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
    }
    else if ( ( WLAN_HDD_SOFTAP== pAdapter->device_mode ) ||
              ( WLAN_HDD_P2P_GO == pAdapter->device_mode )
            )
    {
        WLANSAP_DeRegisterMgmtFrame(
#ifdef WLAN_FEATURE_MBSSID
                WLAN_HDD_GET_SAP_CTX_PTR(pAdapter),
#else
                (WLAN_HDD_GET_CTX(pAdapter))->pvosContext,
#endif
                (SIR_MAC_MGMT_FRAME << 2) | ( SIR_MAC_MGMT_PROBE_REQ << 4),
                NULL, 0 );

    }
    mutex_lock(&cfgState->remain_on_chan_ctx_lock);
    if(pRemainChanCtx->action_pkt_buff.frame_ptr != NULL
       && pRemainChanCtx->action_pkt_buff.frame_length != 0 )
    {
        vos_mem_free(pRemainChanCtx->action_pkt_buff.frame_ptr);
        pRemainChanCtx->action_pkt_buff.frame_ptr = NULL;
        pRemainChanCtx->action_pkt_buff.frame_length = 0;
    }
    vos_mem_free( pRemainChanCtx );
    mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
    complete(&pAdapter->cancel_rem_on_chan_var);
    if (eHAL_STATUS_SUCCESS != status)
        complete(&pAdapter->rem_on_chan_ready_event);
    return eHAL_STATUS_SUCCESS;
}

void wlan_hdd_cancel_existing_remain_on_channel(hdd_adapter_t *pAdapter)
{
    hdd_cfg80211_state_t *cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );
    hdd_remain_on_chan_ctx_t *pRemainChanCtx;
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
    unsigned long rc;

    mutex_lock(&cfgState->remain_on_chan_ctx_lock);
    if(cfgState->remain_on_chan_ctx != NULL)
    {
        hddLog(LOGE, "Cancel Existing Remain on Channel");

        if (VOS_TIMER_STATE_RUNNING == vos_timer_getCurrentState(
                    &cfgState->remain_on_chan_ctx->hdd_remain_on_chan_timer))
        {
            if (!VOS_IS_STATUS_SUCCESS(
                        vos_timer_stop(&cfgState->remain_on_chan_ctx->
                                 hdd_remain_on_chan_timer)))
                hddLog( LOGE, FL("Failed to stop hdd_remain_on_chan_timer"));
        }
        pRemainChanCtx = cfgState->remain_on_chan_ctx;
        if (NULL == pRemainChanCtx)
        {
            mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
            hddLog(LOGE, FL("pRemainChanCtx is NULL"));
            return;
        }
        if (pRemainChanCtx->hdd_remain_on_chan_cancel_in_progress == TRUE)
        {
            mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
            hddLog(LOGE,
                    "ROC timer cancellation in progress,"
                    " wait for completion");
            rc = wait_for_completion_timeout(&pAdapter->cancel_rem_on_chan_var,
                               msecs_to_jiffies(WAIT_CANCEL_REM_CHAN));
            if (!rc) {
                hddLog(LOGE,
                        "%s:wait on cancel_rem_on_chan_var timed out",
                         __func__);
            }
            return;
        }
        pRemainChanCtx->hdd_remain_on_chan_cancel_in_progress = TRUE;
        mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
        /* Wait till remain on channel ready indication before issuing cancel
         * remain on channel request, otherwise if remain on channel not
         * received and if the driver issues cancel remain on channel then lim
         * will be in unknown state.
         */
        rc = wait_for_completion_timeout(&pAdapter->rem_on_chan_ready_event,
               msecs_to_jiffies(WAIT_REM_CHAN_READY));
        if (!rc) {
            hddLog( LOGE,
                    "%s: timeout waiting for remain on channel ready indication",
                    __func__);
            vos_flush_logs(WLAN_LOG_TYPE_FATAL,
                           WLAN_LOG_INDICATOR_HOST_DRIVER,
                           WLAN_LOG_REASON_HDD_TIME_OUT,
                           DUMP_VOS_TRACE);
        }

        INIT_COMPLETION(pAdapter->cancel_rem_on_chan_var);

        /* Issue abort remain on chan request to sme.
         * The remain on channel callback will make sure the remain_on_chan
         * expired event is sent.
        */
        if ( ( WLAN_HDD_INFRA_STATION == pAdapter->device_mode ) ||
             ( WLAN_HDD_P2P_CLIENT == pAdapter->device_mode ) ||
             ( WLAN_HDD_P2P_DEVICE == pAdapter->device_mode )
           )
        {
            hdd_delete_all_action_frame_cookies(pAdapter);
            sme_CancelRemainOnChannel( WLAN_HDD_GET_HAL_CTX( pAdapter ),
                                                     pAdapter->sessionId );
        }
        else if ( (WLAN_HDD_SOFTAP== pAdapter->device_mode) ||
                  (WLAN_HDD_P2P_GO == pAdapter->device_mode)
                )
        {
            WLANSAP_CancelRemainOnChannel(
#ifdef WLAN_FEATURE_MBSSID
                                     WLAN_HDD_GET_SAP_CTX_PTR(pAdapter));
#else
                                     (WLAN_HDD_GET_CTX(pAdapter))->pvosContext);
#endif
        }

        rc = wait_for_completion_timeout(&pAdapter->cancel_rem_on_chan_var,
               msecs_to_jiffies(WAIT_CANCEL_REM_CHAN));

        if (!rc) {
            hddLog( LOGE,
                    "%s: timeout waiting for cancel remain on channel ready"
                    " indication",
                    __func__);
        }
        vos_runtime_pm_allow_suspend(pHddCtx->runtime_context.roc);
        hdd_allow_suspend(WIFI_POWER_EVENT_WAKELOCK_ROC);
    } else
        mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
}

int wlan_hdd_check_remain_on_channel(hdd_adapter_t *pAdapter)
{
   int status = 0;
   hdd_cfg80211_state_t *cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );

   if(WLAN_HDD_P2P_GO != pAdapter->device_mode)
   {
     //Cancel Existing Remain On Channel
     //If no action frame is pending
     if( cfgState->remain_on_chan_ctx != NULL)
     {
        //Check whether Action Frame is pending or not
        if( cfgState->buf == NULL)
        {
           wlan_hdd_cancel_existing_remain_on_channel(pAdapter);
        }
        else
        {
           hddLog( LOG1, "Cannot Cancel Existing Remain on Channel");
           status = -EBUSY;
        }
     }
   }
   return status;
}
/* Clean up RoC context at hdd_stop_adapter*/
void wlan_hdd_cleanup_remain_on_channel_ctx(hdd_adapter_t *pAdapter)
{
    unsigned long rc;
    v_U8_t retry = 0;
    hdd_cfg80211_state_t *cfgState = WLAN_HDD_GET_CFG_STATE_PTR(pAdapter);
    hdd_remain_on_chan_ctx_t *roc_ctx;

    mutex_lock(&cfgState->remain_on_chan_ctx_lock);
    while (pAdapter->is_roc_inprogress)
    {
        mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: ROC in progress for session %d!!!",
                      __func__, pAdapter->sessionId);
        msleep(500);
        if (retry++ > 3) {
           VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                     "%s: ROC completion is not received.!!!", __func__);

           mutex_lock(&cfgState->remain_on_chan_ctx_lock);
           roc_ctx = cfgState->remain_on_chan_ctx;
           if (roc_ctx == NULL)
           {
               mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
               hddLog(LOG1, FL("roc_ctx is NULL!"));
               return;
           }
           if (roc_ctx->hdd_remain_on_chan_cancel_in_progress == true) {
                mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
                hddLog(LOG1, FL("roc cancel already in progress"));
                /*
                 * Since a cancel roc is already issued and is
                 * in progress, we need not send another
                 * cancel roc again. Instead we can just wait
                 * for cancel roc completion
                 */
                goto wait;
           }
           mutex_unlock(&cfgState->remain_on_chan_ctx_lock);

           INIT_COMPLETION(pAdapter->cancel_rem_on_chan_var);

           if (pAdapter->device_mode == WLAN_HDD_P2P_GO)
           {
               WLANSAP_CancelRemainOnChannel(
                         (WLAN_HDD_GET_CTX(pAdapter))->pvosContext);
           } else if (pAdapter->device_mode == WLAN_HDD_P2P_CLIENT ||
                pAdapter->device_mode == WLAN_HDD_P2P_DEVICE)
           {
               hdd_delete_all_action_frame_cookies(pAdapter);
               sme_CancelRemainOnChannel(WLAN_HDD_GET_HAL_CTX(pAdapter),
                                     pAdapter->sessionId);
           }
wait:
           rc = wait_for_completion_timeout(&pAdapter->cancel_rem_on_chan_var,
                                             msecs_to_jiffies(WAIT_CANCEL_REM_CHAN));
           if (!rc) {
                VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                            "%s: Timeout occurred while waiting for RoC Cancellation" ,
                              __func__);
                mutex_lock(&cfgState->remain_on_chan_ctx_lock);
                roc_ctx = cfgState->remain_on_chan_ctx;
                if (roc_ctx != NULL)
                {
                     cfgState->remain_on_chan_ctx = NULL;
                     if (!VOS_IS_STATUS_SUCCESS(vos_timer_stop(
                                     &roc_ctx->hdd_remain_on_chan_timer)))
                         hddLog( LOGE, FL("Failed to stop hdd_remain_on_chan_timer"));
                     if (!VOS_IS_STATUS_SUCCESS(vos_timer_destroy
                                 (&roc_ctx->hdd_remain_on_chan_timer)))
                         hddLog( LOGE, FL(
                                     "Failed to destroy hdd_remain_on_chan_timer"));
                     if (roc_ctx->action_pkt_buff.frame_ptr != NULL
                           && roc_ctx->action_pkt_buff.frame_length != 0)
                     {
                         vos_mem_free(roc_ctx->action_pkt_buff.frame_ptr);
                         roc_ctx->action_pkt_buff.frame_ptr = NULL;
                         roc_ctx->action_pkt_buff.frame_length = 0;
                     }
                     vos_mem_free(roc_ctx);
                     pAdapter->is_roc_inprogress = FALSE;
                }
                mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
            }
            /* hold the lock before break from the loop */
            mutex_lock(&cfgState->remain_on_chan_ctx_lock);
            break;
       }
       mutex_lock(&cfgState->remain_on_chan_ctx_lock);
   } /* end of while */
   mutex_unlock(&cfgState->remain_on_chan_ctx_lock);

}

void wlan_hdd_remain_on_chan_timeout(void *data)
{
    hdd_adapter_t *pAdapter = (hdd_adapter_t *)data;
    hdd_context_t *pHddCtx;
    hdd_remain_on_chan_ctx_t *pRemainChanCtx;
    hdd_cfg80211_state_t *cfgState;

    if ((NULL == pAdapter) || (WLAN_HDD_ADAPTER_MAGIC != pAdapter->magic)) {
        hddLog(LOGE, FL("pAdapter is invalid %pK !!!"), pAdapter);
        return;
    }

    pHddCtx = WLAN_HDD_GET_CTX(pAdapter);

    cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );
    mutex_lock(&cfgState->remain_on_chan_ctx_lock);
    pRemainChanCtx = cfgState->remain_on_chan_ctx;

    if(NULL == pRemainChanCtx)
    {
        mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
        hddLog( LOGE,"%s: No Remain on channel is pending", __func__);
        return;
    }

    if ( TRUE == pRemainChanCtx->hdd_remain_on_chan_cancel_in_progress )
    {
        mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
        hddLog( LOGE, FL("Cancellation already in progress"));
        return;
    }

    pRemainChanCtx->hdd_remain_on_chan_cancel_in_progress = TRUE;
    mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
    hddLog( LOG1,"%s: Cancel Remain on Channel on timeout", __func__);

    if ( ( WLAN_HDD_INFRA_STATION == pAdapter->device_mode ) ||
          ( WLAN_HDD_P2P_CLIENT == pAdapter->device_mode ) ||
           ( WLAN_HDD_P2P_DEVICE == pAdapter->device_mode )
       )
    {
        hdd_delete_all_action_frame_cookies(pAdapter);
        sme_CancelRemainOnChannel( WLAN_HDD_GET_HAL_CTX( pAdapter ),
                                                     pAdapter->sessionId );
    }
    else if ( (WLAN_HDD_SOFTAP== pAdapter->device_mode) ||
                  (WLAN_HDD_P2P_GO == pAdapter->device_mode)
                )
    {
         WLANSAP_CancelRemainOnChannel(
                         (WLAN_HDD_GET_CTX(pAdapter))->pvosContext);
    }

    wlan_hdd_start_stop_tdls_source_timer(pHddCtx, eTDLS_SUPPORT_ENABLED);
    vos_runtime_pm_allow_suspend(pHddCtx->runtime_context.roc);
    hdd_allow_suspend(WIFI_POWER_EVENT_WAKELOCK_ROC);
}

static int wlan_hdd_execute_remain_on_channel(hdd_adapter_t *pAdapter,
                                  hdd_remain_on_chan_ctx_t *pRemainChanCtx)
{
    hdd_cfg80211_state_t *cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );
    VOS_STATUS vos_status = VOS_STATUS_E_FAILURE;
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX( pAdapter );
    hdd_adapter_list_node_t *pAdapterNode = NULL, *pNext = NULL;
    hdd_adapter_t *pAdapter_temp;
    VOS_STATUS status;
    v_BOOL_t isGoPresent = VOS_FALSE;
    unsigned int duration;

    mutex_lock(&cfgState->remain_on_chan_ctx_lock);
    if (pAdapter->is_roc_inprogress == TRUE) {
        mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
        hddLog(VOS_TRACE_LEVEL_ERROR,
               FL("remain on channel request is in execution"));
        return -EBUSY;
    }
    cfgState->remain_on_chan_ctx = pRemainChanCtx;
    cfgState->current_freq = pRemainChanCtx->chan.center_freq;
    pAdapter->is_roc_inprogress = TRUE;
    mutex_unlock(&cfgState->remain_on_chan_ctx_lock);

    /* Initialize Remain on chan timer */
    vos_status = vos_timer_init(&pRemainChanCtx->hdd_remain_on_chan_timer,
                                VOS_TIMER_TYPE_SW,
                                wlan_hdd_remain_on_chan_timeout,
                                pAdapter);
    if (vos_status != VOS_STATUS_SUCCESS)
    {
         hddLog(VOS_TRACE_LEVEL_ERROR,
             FL("Not able to initialize remain_on_chan timer"));
         mutex_lock(&cfgState->remain_on_chan_ctx_lock);
         cfgState->remain_on_chan_ctx = NULL;
         pAdapter->is_roc_inprogress = FALSE;
         mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
         vos_mem_free(pRemainChanCtx);
         return -EINVAL;
    }

    status =  hdd_get_front_adapter ( pHddCtx, &pAdapterNode );
    while ( NULL != pAdapterNode && VOS_STATUS_SUCCESS == status )
    {
        pAdapter_temp = pAdapterNode->pAdapter;
        if(pAdapter_temp->device_mode == WLAN_HDD_P2P_GO)
        {
            isGoPresent = VOS_TRUE;
        }
        status = hdd_get_next_adapter ( pHddCtx, pAdapterNode, &pNext );
        pAdapterNode = pNext;
    }

    //Extending duration for proactive extension logic for RoC
    duration = pRemainChanCtx->duration;
    if (isGoPresent == VOS_TRUE)
         duration = P2P_ROC_DURATION_MULTIPLIER_GO_PRESENT * duration;
    else
         duration = P2P_ROC_DURATION_MULTIPLIER_GO_ABSENT * duration;

    hdd_prevent_suspend(WIFI_POWER_EVENT_WAKELOCK_ROC);
    vos_runtime_pm_prevent_suspend(pHddCtx->runtime_context.roc);
    INIT_COMPLETION(pAdapter->rem_on_chan_ready_event);

    //call sme API to start remain on channel.
    if ( ( WLAN_HDD_INFRA_STATION == pAdapter->device_mode ) ||
         ( WLAN_HDD_P2P_CLIENT == pAdapter->device_mode ) ||
         ( WLAN_HDD_P2P_DEVICE == pAdapter->device_mode )
       )
    {
        tANI_U8 sessionId = pAdapter->sessionId;
        //call sme API to start remain on channel.

        if (eHAL_STATUS_SUCCESS != sme_RemainOnChannel(
                       WLAN_HDD_GET_HAL_CTX(pAdapter), sessionId,
                       pRemainChanCtx->chan.hw_value, duration,
                       wlan_hdd_remain_on_channel_callback, pAdapter,
                       (tANI_U8)(pRemainChanCtx->rem_on_chan_request
                                  == REMAIN_ON_CHANNEL_REQUEST)? TRUE:FALSE)) {
            hddLog(LOGE, FL("sme_RemainOnChannel returned failure"));
            mutex_lock(&cfgState->remain_on_chan_ctx_lock);
            pAdapter->is_roc_inprogress = FALSE;
            pRemainChanCtx = cfgState->remain_on_chan_ctx;
            hddLog( LOG1, FL(
                        "Freeing ROC ctx cfgState->remain_on_chan_ctx=%pK"),
                         cfgState->remain_on_chan_ctx);
            if (pRemainChanCtx)
            {
                if (!VOS_IS_STATUS_SUCCESS(vos_timer_destroy
                            (&pRemainChanCtx->hdd_remain_on_chan_timer)))
                    hddLog( LOGE, FL(
                        "Failed to destroy hdd_remain_on_chan_timer"));
                vos_mem_free(pRemainChanCtx);
                cfgState->remain_on_chan_ctx = NULL;
            }
            mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
            vos_runtime_pm_allow_suspend(pHddCtx->runtime_context.roc);
            hdd_allow_suspend(WIFI_POWER_EVENT_WAKELOCK_ROC);
            return -EINVAL;
        }

        mutex_lock(&cfgState->remain_on_chan_ctx_lock);
        pRemainChanCtx = cfgState->remain_on_chan_ctx;
        if ((pRemainChanCtx)&&(REMAIN_ON_CHANNEL_REQUEST ==
            pRemainChanCtx->rem_on_chan_request)) {
            mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
            if (eHAL_STATUS_SUCCESS != sme_RegisterMgmtFrame(
                                         WLAN_HDD_GET_HAL_CTX(pAdapter),
                                         sessionId, (SIR_MAC_MGMT_FRAME << 2) |
                                        (SIR_MAC_MGMT_PROBE_REQ << 4), NULL, 0))
                hddLog(LOGE, FL("sme_RegisterMgmtFrame returned failure"));
        } else {
               mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
        }
    }
    else if ( ( WLAN_HDD_SOFTAP== pAdapter->device_mode ) ||
              ( WLAN_HDD_P2P_GO == pAdapter->device_mode )
            )
    {
        //call sme API to start remain on channel.
        if (VOS_STATUS_SUCCESS != WLANSAP_RemainOnChannel(
#ifdef WLAN_FEATURE_MBSSID
                          WLAN_HDD_GET_SAP_CTX_PTR(pAdapter),
#else
                          (WLAN_HDD_GET_CTX(pAdapter))->pvosContext,
#endif
                          pRemainChanCtx->chan.hw_value, duration,
                          wlan_hdd_remain_on_channel_callback, pAdapter))
        {
           VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                    "%s: WLANSAP_RemainOnChannel returned fail", __func__);

           mutex_lock(&cfgState->remain_on_chan_ctx_lock);
           pAdapter->is_roc_inprogress = FALSE;
           pRemainChanCtx = cfgState->remain_on_chan_ctx;
           hddLog( LOG1, FL(
                        "Freeing ROC ctx cfgState->remain_on_chan_ctx=%pK"),
                         cfgState->remain_on_chan_ctx);
           if (pRemainChanCtx)
           {
                if (!VOS_IS_STATUS_SUCCESS(vos_timer_destroy
                            (&pRemainChanCtx->hdd_remain_on_chan_timer)))
                    hddLog( LOGE, FL(
                         "Failed to destroy hdd_remain_on_chan_timer"));
                vos_mem_free (pRemainChanCtx);
                cfgState->remain_on_chan_ctx = NULL;
           }
           mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
           vos_runtime_pm_allow_suspend(pHddCtx->runtime_context.roc);
           hdd_allow_suspend(WIFI_POWER_EVENT_WAKELOCK_ROC);
           return -EINVAL;
        }


        if (VOS_STATUS_SUCCESS != WLANSAP_RegisterMgmtFrame(
#ifdef WLAN_FEATURE_MBSSID
                    WLAN_HDD_GET_SAP_CTX_PTR(pAdapter),
#else
                    (WLAN_HDD_GET_CTX(pAdapter))->pvosContext,
#endif
                    (SIR_MAC_MGMT_FRAME << 2) | ( SIR_MAC_MGMT_PROBE_REQ << 4),
                    NULL, 0))
        {
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                    "%s: WLANSAP_RegisterMgmtFrame returned fail", __func__);
            WLANSAP_CancelRemainOnChannel(
#ifdef WLAN_FEATURE_MBSSID
                    WLAN_HDD_GET_SAP_CTX_PTR(pAdapter));
#else
                    (WLAN_HDD_GET_CTX(pAdapter))->pvosContext);
#endif
            vos_runtime_pm_allow_suspend(pHddCtx->runtime_context.roc);
            hdd_allow_suspend(WIFI_POWER_EVENT_WAKELOCK_ROC);
            return -EINVAL;
        }

    }
    wlan_hdd_start_stop_tdls_source_timer(pHddCtx, eTDLS_SUPPORT_DISABLED);
    return 0;
}

/**
 * wlan_hdd_roc_request_enqueue() - enqueue remain on channel request
 * @adapter: Pointer to the adapter
 * @remain_chan_ctx: Pointer to the remain on channel context
 *
 * Return: 0 on success, error number otherwise
 */
static int wlan_hdd_roc_request_enqueue(hdd_adapter_t *adapter,
			hdd_remain_on_chan_ctx_t *remain_chan_ctx)
{
	hdd_context_t *hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	hdd_roc_req_t *hdd_roc_req;
	VOS_STATUS status;

	/*
	 * "Driver is busy" OR "there is already RoC request inside the queue"
	 * so enqueue this RoC Request and execute sequentially later.
	 */

	hdd_roc_req = vos_mem_malloc(sizeof(*hdd_roc_req));

	if (NULL == hdd_roc_req) {
		hddLog(LOGP, FL("malloc failed for roc req context"));
		return -ENOMEM;
	}

	hdd_roc_req->pAdapter = adapter;
	hdd_roc_req->pRemainChanCtx = remain_chan_ctx;

	/* Enqueue this RoC request */
	adf_os_spin_lock(&hdd_ctx->hdd_roc_req_q.lock);
	status = hdd_list_insert_back(&hdd_ctx->hdd_roc_req_q,
					&hdd_roc_req->node);
	adf_os_spin_unlock(&hdd_ctx->hdd_roc_req_q.lock);

	if (VOS_STATUS_SUCCESS != status) {
		hddLog(LOGP, FL("Not able to enqueue RoC Req context"));
		vos_mem_free(hdd_roc_req);
		return -EINVAL;
	}

	return 0;
}

/**
 * wlan_hdd_indicate_roc_drop() - Indicate roc drop to userspace
 * @adapter: HDD adapter
 * @ctx: Remain on channel context
 *
 * Send remain on channel ready and cancel event for the queued
 * roc that is being dropped. This will ensure that the userspace
 * will send more roc requests. If this drop is not indicated to
 * userspace, subsequent roc will not be sent to the driver since
 * the userspace times out waiting for the remain on channel ready
 * event.
 *
 * Return: None
 */
void wlan_hdd_indicate_roc_drop(hdd_adapter_t *adapter,
		hdd_remain_on_chan_ctx_t *ctx)
{
	hddLog(LOG1, FL("indicate roc drop to userspace"));
	cfg80211_ready_on_channel(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
			adapter->dev->ieee80211_ptr,
#else
			adapter->dev,
#endif
			(uintptr_t)ctx,
			&ctx->chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
			ctx->chan_type,
#endif
			ctx->duration, GFP_KERNEL);

	cfg80211_remain_on_channel_expired(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
			ctx->dev->ieee80211_ptr,
#else
			ctx->dev,
#endif
			ctx->cookie,
			&ctx->chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 8, 0))
			ctx->chan_type,
#endif
			GFP_KERNEL);
}

/**
 * wlan_hdd_roc_request_dequeue() - dequeue remain on channel request
 * @work: Pointer to work queue struct
 *
 * Return: none
 */
void wlan_hdd_roc_request_dequeue(struct work_struct *work)
{
	VOS_STATUS status;
	int ret = 0;
	hdd_roc_req_t *hdd_roc_req;
	hdd_context_t *hdd_ctx =
			container_of(work, hdd_context_t, rocReqWork.work);

	if (0 != (wlan_hdd_validate_context(hdd_ctx)))
		return;

	/*
	 * The queued roc requests is dequeued and processed one at a time.
	 * Callback 'wlan_hdd_remain_on_channel_callback' ensures
	 * that any pending roc in the queue will be scheduled
	 * on the current roc completion by scheduling the work queue.
	 */
	adf_os_spin_lock(&hdd_ctx->hdd_roc_req_q.lock);
	if (list_empty(&hdd_ctx->hdd_roc_req_q.anchor)) {
		adf_os_spin_unlock(&hdd_ctx->hdd_roc_req_q.lock);
		return;
	}
	status = hdd_list_remove_front(&hdd_ctx->hdd_roc_req_q,
			(hdd_list_node_t **) &hdd_roc_req);
	adf_os_spin_unlock(&hdd_ctx->hdd_roc_req_q.lock);
	if (VOS_STATUS_SUCCESS != status) {
		hddLog(LOG1, FL("unable to remove roc element from list"));
		return;
	}
	ret = wlan_hdd_execute_remain_on_channel(
			hdd_roc_req->pAdapter,
			hdd_roc_req->pRemainChanCtx);
	if (ret == -EBUSY) {
		hddLog(VOS_TRACE_LEVEL_ERROR,
				FL("dropping RoC request"));
		wlan_hdd_indicate_roc_drop(hdd_roc_req->pAdapter,
					hdd_roc_req->pRemainChanCtx);
		vos_mem_free(hdd_roc_req->pRemainChanCtx);
	}
	vos_mem_free(hdd_roc_req);
}

static int wlan_hdd_request_remain_on_channel( struct wiphy *wiphy,
                                   struct net_device *dev,
                                   struct ieee80211_channel *chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                                   enum nl80211_channel_type channel_type,
#endif
                                   unsigned int duration, u64 *cookie,
                                   rem_on_channel_request_type_t request_type )
{
    hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    hdd_context_t *pHddCtx = NULL;
    hdd_remain_on_chan_ctx_t *pRemainChanCtx;
    v_BOOL_t isBusy = VOS_FALSE;
    v_SIZE_t size = 0;
    hdd_adapter_t *sta_adapter;
    int ret = 0;
    int status = 0;
#ifdef FEATURE_WLAN_DISABLE_CHANNEL_SWITCH
    uint8_t channel;
#endif
    hddLog(LOG1, FL("Device_mode %s(%d)"),
           hdd_device_mode_to_string(pAdapter->device_mode),
           pAdapter->device_mode);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
    hddLog( LOG1,
        "chan(hw_val)0x%x chan(centerfreq) %d chan type 0x%x, duration %d",
        chan->hw_value, chan->center_freq, channel_type, duration );
#else
    hddLog( LOG1,
        "chan(hw_val)0x%x chan(centerfreq) %d, duration %d",
        chan->hw_value, chan->center_freq, duration );
#endif

    /*
     * When P2P-GO and if we are trying to unload the driver then
     * wlan driver is keep on receiving the remain on channel command
     * and which is resulting in crash. So not allowing any remain on
     * channel requets when Load/Unload is in progress
     */
    pHddCtx = WLAN_HDD_GET_CTX(pAdapter);
    ret = wlan_hdd_validate_context(pHddCtx);
    if (0 != ret)
        return ret;

    if (hdd_isConnectionInProgress((hdd_context_t *)pAdapter->pHddCtx, NULL,
                                    NULL)) {
        hddLog(LOGE, FL("Connection is in progress"));
        isBusy = VOS_TRUE;
    }
#ifdef FEATURE_WLAN_DISABLE_CHANNEL_SWITCH
    channel = vos_freq_to_chan(chan->center_freq);
    if (!vos_is_chan_ok_for_dnbs(channel)) {
        hddLog(LOGE, FL("chan-%d is not valid for DNBS"), channel);
        return 0;
    }
#endif
    pRemainChanCtx = vos_mem_malloc(sizeof(hdd_remain_on_chan_ctx_t));
    if (NULL == pRemainChanCtx) {
        hddLog(VOS_TRACE_LEVEL_FATAL,
             "%s: Not able to allocate memory for Channel context",
                                         __func__);
        return -ENOMEM;
    }

    vos_mem_zero(pRemainChanCtx, sizeof(*pRemainChanCtx));
    vos_mem_copy(&pRemainChanCtx->chan, chan,
                   sizeof(struct ieee80211_channel));

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
    pRemainChanCtx->chan_type = channel_type;
#endif
    pRemainChanCtx->duration = duration;
    pRemainChanCtx->p2pRemOnChanTimeStamp = vos_timer_get_system_time();
    pRemainChanCtx->dev = dev;
    *cookie = (uintptr_t) pRemainChanCtx;
    pRemainChanCtx->cookie = *cookie;
    pRemainChanCtx->rem_on_chan_request = request_type;

    pRemainChanCtx->action_pkt_buff.freq = 0;
    pRemainChanCtx->action_pkt_buff.frame_ptr = NULL;
    pRemainChanCtx->action_pkt_buff.frame_length = 0;
    pRemainChanCtx->hdd_remain_on_chan_cancel_in_progress = FALSE;

    if (REMAIN_ON_CHANNEL_REQUEST == request_type) {
        sta_adapter = hdd_get_adapter(pHddCtx, WLAN_HDD_INFRA_STATION);
        if ((NULL != sta_adapter)&&
               hdd_connIsConnected(WLAN_HDD_GET_STATION_CTX_PTR(sta_adapter))) {
            if (pAdapter->lastRocTs !=0 &&
                    ((vos_timer_get_system_time() - pAdapter->lastRocTs )
                     < pHddCtx->cfg_ini->p2p_listen_defer_interval)) {
                if (pRemainChanCtx->duration > HDD_P2P_MAX_ROC_DURATION)
                    pRemainChanCtx->duration = HDD_P2P_MAX_ROC_DURATION;

                wlan_hdd_roc_request_enqueue(pAdapter, pRemainChanCtx);
                queue_delayed_work(system_freezable_power_efficient_wq,
                                   &pHddCtx->rocReqWork,
                msecs_to_jiffies(pHddCtx->cfg_ini->p2p_listen_defer_interval));
                hddLog(LOG1, "Defer interval is %hu, pAdapter %pK",
                       pHddCtx->cfg_ini->p2p_listen_defer_interval, pAdapter);
                return 0;
            }
        }
    }

    /* Check roc_req_Q has pending RoC Request or not */
    hdd_list_size(&(pHddCtx->hdd_roc_req_q), &size);

    if ((isBusy == VOS_FALSE) && (!size)) {
        /* Media is free and no RoC request is in queue, execute directly */
        status = wlan_hdd_execute_remain_on_channel(pAdapter,
                                                    pRemainChanCtx);
        if (status == -EBUSY) {
            if (wlan_hdd_roc_request_enqueue(pAdapter, pRemainChanCtx)) {
                vos_mem_free(pRemainChanCtx);
                return -EAGAIN;
            }
        }
        return 0;
    } else {
        if (wlan_hdd_roc_request_enqueue(pAdapter, pRemainChanCtx)) {
            vos_mem_free(pRemainChanCtx);
            return -EAGAIN;
        }
    }

    /*
     * If a connection is not in progress (isBusy), before scheduling
     * the work queue it is necessary to check if a roc in in progress
     * or not because: if an roc is in progress, the dequeued roc
     * that will be processed will be dropped. To ensure that this new
     * roc request is not dropped, it is suggested to check if an roc
     * is in progress or not. The existing roc completion will provide
     * the trigger to dequeue the next roc request.
     */
    if (isBusy == VOS_FALSE && pAdapter->is_roc_inprogress == false) {
        hddLog(LOG1, FL("scheduling delayed work: no connection/roc active"));
        queue_delayed_work(system_freezable_power_efficient_wq,
                           &pHddCtx->rocReqWork, 0);
    }
    return 0;
}

int __wlan_hdd_cfg80211_remain_on_channel( struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                                struct wireless_dev *wdev,
#else
                                struct net_device *dev,
#endif
                                struct ieee80211_channel *chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                                enum nl80211_channel_type channel_type,
#endif
                                unsigned int duration, u64 *cookie )
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
    struct net_device *dev = wdev->netdev;
#endif
    hdd_adapter_t *pAdapter;
    hdd_context_t *hdd_ctx;
    int ret;

    ENTER();

    if (VOS_FTM_MODE == hdd_get_conparam()) {
        hddLog(LOGE, FL("Command not allowed in FTM mode"));
        return -EINVAL;
    }

    pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    hdd_ctx = WLAN_HDD_GET_CTX(pAdapter);
    ret = wlan_hdd_validate_context(hdd_ctx);
    if (0 != ret)
        return ret;

    MTRACE(vos_trace(VOS_MODULE_ID_HDD,
                     TRACE_CODE_HDD_REMAIN_ON_CHANNEL,
                     pAdapter->sessionId, REMAIN_ON_CHANNEL_REQUEST));
    ret = wlan_hdd_request_remain_on_channel(wiphy, dev,
                                        chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                                        channel_type,
#endif
                                        duration, cookie,
                                        REMAIN_ON_CHANNEL_REQUEST);
   EXIT();
   return ret;
}

int wlan_hdd_cfg80211_remain_on_channel( struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                                struct wireless_dev *wdev,
#else
                                struct net_device *dev,
#endif
                                struct ieee80211_channel *chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                                enum nl80211_channel_type channel_type,
#endif
                                unsigned int duration, u64 *cookie )
{
   int ret;

   vos_ssr_protect(__func__);
   ret = __wlan_hdd_cfg80211_remain_on_channel(wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                                               wdev,
#else
                                               dev,
#endif
                                               chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                                               channel_type,
#endif
                                               duration, cookie);
   vos_ssr_unprotect(__func__);

   return ret;
}


void hdd_remainChanReadyHandler( hdd_adapter_t *pAdapter )
{
    hdd_cfg80211_state_t *cfgState = NULL;
    hdd_remain_on_chan_ctx_t* pRemainChanCtx = NULL;
    VOS_STATUS status;

    if (NULL == pAdapter)
    {
        hddLog(LOGE, FL("pAdapter is NULL"));
        return;
    }
    cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );
    hddLog( LOG1, "Ready on chan ind");

    pAdapter->startRocTs = vos_timer_get_system_time();
    mutex_lock(&cfgState->remain_on_chan_ctx_lock);
    pRemainChanCtx = cfgState->remain_on_chan_ctx;
    if( pRemainChanCtx != NULL )
    {
        MTRACE(vos_trace(VOS_MODULE_ID_HDD,
                         TRACE_CODE_HDD_REMAINCHANREADYHANDLER,
                         pAdapter->sessionId, pRemainChanCtx->duration));

        // Removing READY_EVENT_PROPOGATE_TIME from current time which gives
        // more accurate Remain on Channel start time.
        pRemainChanCtx->p2pRemOnChanTimeStamp =
                      vos_timer_get_system_time() - READY_EVENT_PROPOGATE_TIME;

        //start timer for actual duration
        if(VOS_TIMER_STATE_RUNNING ==
           vos_timer_getCurrentState(&pRemainChanCtx->hdd_remain_on_chan_timer))
        {
            hddLog( LOGE, "Timer Started before ready event!!!");
            if (!VOS_IS_STATUS_SUCCESS(vos_timer_stop(
                            &pRemainChanCtx->hdd_remain_on_chan_timer)))
                hddLog( LOGE, FL("Failed to stop hdd_remain_on_chan_timer"));
        }
        status = vos_timer_start(&pRemainChanCtx->hdd_remain_on_chan_timer,
                                (pRemainChanCtx->duration + COMPLETE_EVENT_PROPOGATE_TIME));
        if (status != VOS_STATUS_SUCCESS)
        {
            hddLog( LOGE, "%s: Remain on Channel timer start failed",
                          __func__);
        }

        if( REMAIN_ON_CHANNEL_REQUEST == pRemainChanCtx->rem_on_chan_request)
        {
            cfg80211_ready_on_channel(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                               pAdapter->dev->ieee80211_ptr,
#else
                               pAdapter->dev,
#endif
                               (uintptr_t)pRemainChanCtx,
                               &pRemainChanCtx->chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                               pRemainChanCtx->chan_type,
#endif
                               pRemainChanCtx->duration, GFP_KERNEL );
        }
        else if( OFF_CHANNEL_ACTION_TX == pRemainChanCtx->rem_on_chan_request)
        {
            complete(&pAdapter->offchannel_tx_event);
        }

        // Check for cached action frame
        if(pRemainChanCtx->action_pkt_buff.frame_length != 0)
        {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)) || defined(WITH_BACKPORTS)
          cfg80211_rx_mgmt(pAdapter->dev->ieee80211_ptr,
                      pRemainChanCtx->action_pkt_buff.freq, 0,
                      pRemainChanCtx->action_pkt_buff.frame_ptr,
                      pRemainChanCtx->action_pkt_buff.frame_length,
                      NL80211_RXMGMT_FLAG_ANSWERED);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,0))
          cfg80211_rx_mgmt(pAdapter->dev->ieee80211_ptr,
                      pRemainChanCtx->action_pkt_buff.freq, 0,
                      pRemainChanCtx->action_pkt_buff.frame_ptr,
                      pRemainChanCtx->action_pkt_buff.frame_length,
                      NL80211_RXMGMT_FLAG_ANSWERED, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
          cfg80211_rx_mgmt( pAdapter->dev->ieee80211_ptr,pRemainChanCtx->action_pkt_buff.freq, 0,
                      pRemainChanCtx->action_pkt_buff.frame_ptr,
                      pRemainChanCtx->action_pkt_buff.frame_length,
                      GFP_ATOMIC );
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
          cfg80211_rx_mgmt( pAdapter->dev, pRemainChanCtx->action_pkt_buff.freq, 0,
                      pRemainChanCtx->action_pkt_buff.frame_ptr,
                      pRemainChanCtx->action_pkt_buff.frame_length,
                      GFP_ATOMIC );
#else
          cfg80211_rx_mgmt( pAdapter->dev, pRemainChanCtx->action_pkt_buff.freq,
                      pRemainChanCtx->action_pkt_buff.frame_ptr,
                      pRemainChanCtx->action_pkt_buff.frame_length,
                      GFP_ATOMIC );
#endif /* LINUX_VERSION_CODE */

          vos_mem_free(pRemainChanCtx->action_pkt_buff.frame_ptr);
          pRemainChanCtx->action_pkt_buff.frame_length = 0;
          pRemainChanCtx->action_pkt_buff.freq = 0;
          pRemainChanCtx->action_pkt_buff.frame_ptr = NULL;
        }
        complete(&pAdapter->rem_on_chan_ready_event);
    }
    else
    {
        hddLog( LOGW, "%s: No Pending Remain on channel Request", __func__);
    }
    mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
    return;
}

int __wlan_hdd_cfg80211_cancel_remain_on_channel( struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                                                struct wireless_dev *wdev,
#else
                                                struct net_device *dev,
#endif
                                                u64 cookie )
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
    struct net_device *dev = wdev->netdev;
#endif
    hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    hdd_cfg80211_state_t *cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );
    hdd_remain_on_chan_ctx_t *pRemainChanCtx;
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX( pAdapter );
    int status;
    unsigned long rc;
    hdd_list_node_t *tmp, *q;
    hdd_roc_req_t *curr_roc_req;

    ENTER();

    status = wlan_hdd_validate_context(pHddCtx);
    if (0 != status)
        return status;

    if (VOS_FTM_MODE == hdd_get_conparam()) {
        hddLog(LOGE, FL("Command not allowed in FTM mode"));
        return -EINVAL;
    }

    /* Remove RoC request inside queue */
    adf_os_spin_lock(&pHddCtx->hdd_roc_req_q.lock);
    list_for_each_safe(tmp, q, &pHddCtx->hdd_roc_req_q.anchor) {
        curr_roc_req = list_entry(tmp, hdd_roc_req_t, node);
        if ((uintptr_t)curr_roc_req->pRemainChanCtx == cookie) {
            status = hdd_list_remove_node(&pHddCtx->hdd_roc_req_q,
                                   (hdd_list_node_t*)curr_roc_req);
            adf_os_spin_unlock(&pHddCtx->hdd_roc_req_q.lock);
            if (status == VOS_STATUS_SUCCESS) {
                vos_mem_free(curr_roc_req->pRemainChanCtx);
                vos_mem_free(curr_roc_req);
            }
            return 0;
        }
    }
    adf_os_spin_unlock(&pHddCtx->hdd_roc_req_q.lock);

    /* FIXME cancel currently running remain on chan.
     * Need to check cookie and cancel accordingly
     */
    mutex_lock(&cfgState->remain_on_chan_ctx_lock);
    pRemainChanCtx = cfgState->remain_on_chan_ctx;

    if (pRemainChanCtx) {
       hddLog(LOGE,
             "action_cookie = %08llx, roc cookie = %08llx, cookie = %08llx",
             cfgState->action_cookie, pRemainChanCtx->cookie, cookie);

       if (pRemainChanCtx->cookie == cookie) {
          /* request to cancel on-going roc */
           if (cfgState->buf) {
              /* Tx frame pending */
               if (cfgState->action_cookie != cookie) {
                  hddLog( LOGE,
                        FL("Cookie matched with RoC cookie but not with tx cookie, indicate expired event for roc"));
                  /* RoC was extended to accomodate the tx frame */
                  if (REMAIN_ON_CHANNEL_REQUEST ==
                                       pRemainChanCtx->rem_on_chan_request) {
                     cfg80211_remain_on_channel_expired(pRemainChanCtx->dev->
                                                        ieee80211_ptr,
                                                        pRemainChanCtx->cookie,
                                                        &pRemainChanCtx->chan,
                                                        GFP_KERNEL);
                   }
                   pRemainChanCtx->rem_on_chan_request = OFF_CHANNEL_ACTION_TX;
                   pRemainChanCtx->cookie = cfgState->action_cookie;
                   return 0;
                }
            }
       } else if (cfgState->buf && cfgState->action_cookie == cookie) {
                 mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
                 hddLog( LOGE,
                       FL("Cookie not matched with RoC cookie but matched with tx cookie, cleanup action frame"));
                 /*free the buf and return 0*/
                 hdd_cleanup_actionframe(pHddCtx, pAdapter);
                 return 0;
	 } else {
		 mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
		 hddLog( LOGE, FL("No matching cookie"));
		 return -EINVAL;
         }
    } else {
	    mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
	    hddLog( LOGE, FL("RoC context is NULL, return success"));
	    return 0;
    }

    if (NULL != cfgState->remain_on_chan_ctx)
    {
        if (!VOS_IS_STATUS_SUCCESS(vos_timer_stop(
                    &cfgState->remain_on_chan_ctx->hdd_remain_on_chan_timer)))
            hddLog( LOGE, FL("Failed to stop hdd_remain_on_chan_timer"));
        if (TRUE == pRemainChanCtx->hdd_remain_on_chan_cancel_in_progress)
        {
            mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
            hddLog( LOG1,
                    FL("ROC timer cancellation in progress,"
                       " wait for completion"));
            rc = wait_for_completion_timeout(
                                             &pAdapter->cancel_rem_on_chan_var,
                                             msecs_to_jiffies(WAIT_CANCEL_REM_CHAN));
            if (!rc) {
                hddLog( LOGE,
                        "%s:wait on cancel_rem_on_chan_var timed out",
                        __func__);
            }
            return 0;
        }
        else
            pRemainChanCtx->hdd_remain_on_chan_cancel_in_progress = TRUE;
    }
    mutex_unlock(&cfgState->remain_on_chan_ctx_lock);

    /* wait until remain on channel ready event received
     * for already issued remain on channel request */
    rc = wait_for_completion_timeout(&pAdapter->rem_on_chan_ready_event,
            msecs_to_jiffies(WAIT_REM_CHAN_READY));
    if (!rc) {
        hddLog( LOGE,
                "%s: timeout waiting for remain on channel ready indication",
                __func__);

        if (pHddCtx->isLogpInProgress)
        {
            VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                      "%s: LOGP in Progress. Ignore!!!", __func__);
            return -EAGAIN;
        }
        vos_flush_logs(WLAN_LOG_TYPE_FATAL,
                       WLAN_LOG_INDICATOR_HOST_DRIVER,
                       WLAN_LOG_REASON_HDD_TIME_OUT,
                       DUMP_VOS_TRACE);
    }
    INIT_COMPLETION(pAdapter->cancel_rem_on_chan_var);
    /* Issue abort remain on chan request to sme.
     * The remain on channel callback will make sure the remain_on_chan
     * expired event is sent.
     */
    if ((WLAN_HDD_INFRA_STATION == pAdapter->device_mode) ||
        (WLAN_HDD_P2P_CLIENT == pAdapter->device_mode) ||
        (WLAN_HDD_P2P_DEVICE == pAdapter->device_mode)) {
        tANI_U8 sessionId = pAdapter->sessionId;
        hdd_delete_all_action_frame_cookies(pAdapter);
        sme_CancelRemainOnChannel( WLAN_HDD_GET_HAL_CTX( pAdapter ),
                                            sessionId );
    } else if ((WLAN_HDD_SOFTAP== pAdapter->device_mode) ||
              (WLAN_HDD_P2P_GO == pAdapter->device_mode)) {
        WLANSAP_CancelRemainOnChannel(
#ifdef WLAN_FEATURE_MBSSID
                                WLAN_HDD_GET_SAP_CTX_PTR(pAdapter));
#else
                                (WLAN_HDD_GET_CTX(pAdapter))->pvosContext);
#endif

    } else {
       hddLog(LOGE, FL("Invalid device_mode %s(%d)"),
              hdd_device_mode_to_string(pAdapter->device_mode),
              pAdapter->device_mode);
       return -EIO;
    }
    rc = wait_for_completion_timeout(&pAdapter->cancel_rem_on_chan_var,
            msecs_to_jiffies(WAIT_CANCEL_REM_CHAN));
    if (!rc) {
        hddLog( LOGE,
                "%s:wait on cancel_rem_on_chan_var timed out ", __func__);
    }
    vos_runtime_pm_allow_suspend(pHddCtx->runtime_context.roc);
    hdd_allow_suspend(WIFI_POWER_EVENT_WAKELOCK_ROC);
    EXIT();
    return 0;
}

int wlan_hdd_cfg80211_cancel_remain_on_channel( struct wiphy *wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                                                struct wireless_dev *wdev,
#else
                                                struct net_device *dev,
#endif
                                                u64 cookie )
{
    int ret;

    vos_ssr_protect(__func__);
    ret = __wlan_hdd_cfg80211_cancel_remain_on_channel(wiphy,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                                                    wdev,
#else
                                                    dev,
#endif
                                                    cookie);
    vos_ssr_unprotect(__func__);

    return ret;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
int __wlan_hdd_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
                       struct ieee80211_channel *chan, bool offchan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                       enum nl80211_channel_type channel_type,
                       bool channel_type_valid,
#endif
                       unsigned int wait,
                       const u8 *buf, size_t len,  bool no_cck,
                       bool dont_wait_for_ack, u64 *cookie )
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
int __wlan_hdd_mgmt_tx(struct wiphy *wiphy, struct net_device *dev,
                       struct ieee80211_channel *chan, bool offchan,
                       enum nl80211_channel_type channel_type,
                       bool channel_type_valid, unsigned int wait,
                       const u8 *buf, size_t len,  bool no_cck,
                       bool dont_wait_for_ack, u64 *cookie )
#else
int __wlan_hdd_mgmt_tx(struct wiphy *wiphy, struct net_device *dev,
                       struct ieee80211_channel *chan, bool offchan,
                       enum nl80211_channel_type channel_type,
                       bool channel_type_valid, unsigned int wait,
                       const u8 *buf, size_t len, u64 *cookie )
#endif /* LINUX_VERSION_CODE */
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
    struct net_device *dev = wdev->netdev;
#endif
    hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR( dev );
    hdd_cfg80211_state_t *cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );
    hdd_remain_on_chan_ctx_t *pRemainChanCtx;
    hdd_context_t *pHddCtx = WLAN_HDD_GET_CTX( pAdapter );
    tANI_U16 extendedWait = 0;
    tANI_U8 type;
    tANI_U8 subType;
    tActionFrmType actionFrmType;
    bool noack = 0;
    int status;
    unsigned long rc;
    hdd_adapter_t *goAdapter;
    uint8_t home_ch = 0;
    bool enb_random_mac = false;
    uint32_t mgmt_hdr_len = sizeof(struct ieee80211_hdr_3addr);
    eHalStatus hal_status;

    ENTER();

    if (len < mgmt_hdr_len + 1) {
        hddLog(LOGE, FL("Invalid Length"));
        return -EINVAL;
    }

    type = WLAN_HDD_GET_TYPE_FRM_FC(buf[0]);
    subType = WLAN_HDD_GET_SUBTYPE_FRM_FC(buf[0]);

    hddLog(LOG1, FL("wait: %d, offchan: %d"), wait, offchan);
    MTRACE(vos_trace(VOS_MODULE_ID_HDD,
                      TRACE_CODE_HDD_ACTION, pAdapter->sessionId,
                      pAdapter->device_mode ));
    status = wlan_hdd_validate_context(pHddCtx);
    if (0 != status)
        return status;

    if (VOS_FTM_MODE == hdd_get_conparam()) {
        hddLog(LOGE, FL("Command not allowed in FTM mode"));
        return -EINVAL;
    }

    hddLog(LOG1, FL("Device_mode %s(%d) type: %d"),
           hdd_device_mode_to_string(pAdapter->device_mode),
           pAdapter->device_mode, type);

    /* When frame to be transmitted is auth mgmt, then trigger
     * sme_send_mgmt_tx to send auth frame without need for policy manager.
     * Where as wlan_cfg80211_mgmt_tx requires roc and requires approval
     * from policy manager
     */
    if ((WLAN_HDD_INFRA_STATION == pAdapter->device_mode ||
	WLAN_HDD_SOFTAP == pAdapter->device_mode) &&
        (type == SIR_MAC_MGMT_FRAME &&
        subType == SIR_MAC_MGMT_AUTH)) {
        hal_status = sme_send_mgmt_tx(WLAN_HDD_GET_HAL_CTX(pAdapter),
                                     pAdapter->sessionId, buf, len);
        if (HAL_STATUS_SUCCESS(hal_status))
           return 0;
        else
           return -EINVAL;
    }

    if (type == SIR_MAC_MGMT_FRAME && subType == SIR_MAC_MGMT_ACTION &&
        len > IEEE80211_MIN_ACTION_SIZE)
        hddLog(LOG1, FL("category: %d, actionID: %d"),
               buf[WLAN_HDD_PUBLIC_ACTION_FRAME_BODY_OFFSET +
               WLAN_HDD_PUBLIC_ACTION_FRAME_CATEGORY_OFFSET],
               buf[WLAN_HDD_PUBLIC_ACTION_FRAME_BODY_OFFSET +
               WLAN_HDD_PUBLIC_ACTION_FRAME_ACTION_OFFSET]);

#ifdef WLAN_FEATURE_P2P_DEBUG
    if ((type == SIR_MAC_MGMT_FRAME) &&
            (subType == SIR_MAC_MGMT_ACTION) &&
            wlan_hdd_is_type_p2p_action(
             &buf[WLAN_HDD_PUBLIC_ACTION_FRAME_BODY_OFFSET],
             len - mgmt_hdr_len))
    {
        actionFrmType = buf[WLAN_HDD_PUBLIC_ACTION_FRAME_TYPE_OFFSET];
        if(actionFrmType >= MAX_P2P_ACTION_FRAME_TYPE)
        {
            hddLog(VOS_TRACE_LEVEL_ERROR,"[P2P] unknown[%d] ---> OTA to "
                   MAC_ADDRESS_STR, actionFrmType,
                   MAC_ADDR_ARRAY(&buf[WLAN_HDD_80211_FRM_DA_OFFSET]));
        }
        else
        {
            hddLog(VOS_TRACE_LEVEL_ERROR,"[P2P] %s ---> OTA to "
                   MAC_ADDRESS_STR, p2p_action_frame_type[actionFrmType],
                   MAC_ADDR_ARRAY(&buf[WLAN_HDD_80211_FRM_DA_OFFSET]));
            if( (actionFrmType == WLAN_HDD_PROV_DIS_REQ) &&
                (globalP2PConnectionStatus == P2P_NOT_ACTIVE) )
            {
                 globalP2PConnectionStatus = P2P_GO_NEG_PROCESS;
                 hddLog(LOGE,"[P2P State]Inactive state to "
                            "GO negotiation progress state");
            }
            else if( (actionFrmType == WLAN_HDD_GO_NEG_CNF) &&
                (globalP2PConnectionStatus == P2P_GO_NEG_PROCESS) )
            {
                 globalP2PConnectionStatus = P2P_GO_NEG_COMPLETED;
                 hddLog(LOGE,"[P2P State]GO nego progress to GO nego"
                             " completed state");
            }
        }
    }
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0)) || defined(WITH_BACKPORTS)
    noack = dont_wait_for_ack;
#endif

    //If the wait is coming as 0 with off channel set
    //then set the wait to 200 ms
    if (offchan && !wait)
    {
        wait = ACTION_FRAME_DEFAULT_WAIT;
        mutex_lock(&cfgState->remain_on_chan_ctx_lock);
        if (cfgState->remain_on_chan_ctx)
        {
            tANI_U32 current_time = vos_timer_get_system_time();
            int remaining_roc_time =
                       ((int) cfgState->remain_on_chan_ctx->duration -
                                 (current_time - pAdapter->startRocTs));
            if ( remaining_roc_time > ACTION_FRAME_DEFAULT_WAIT)
                wait = remaining_roc_time;
        }
        mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
    }

#ifndef SUPPORT_P2P_BY_ONE_INTF_WLAN
    if ((WLAN_HDD_INFRA_STATION == pAdapter->device_mode) &&
       (type == SIR_MAC_MGMT_FRAME && subType == SIR_MAC_MGMT_PROBE_RSP)) {
        /* Drop Probe response recieved from supplicant in sta mode */
        goto err_rem_channel;
    }
#endif

    //Call sme API to send out a action frame.
    // OR can we send it directly through data path??
    // After tx completion send tx status back.
    if ( ( WLAN_HDD_SOFTAP == pAdapter->device_mode ) ||
         ( WLAN_HDD_P2P_GO == pAdapter->device_mode )
       )
    {
        if (type == SIR_MAC_MGMT_FRAME)
        {
            if (subType == SIR_MAC_MGMT_PROBE_RSP)
            {
                /* Drop Probe response recieved from supplicant, as for GO and
                   SAP PE itself sends probe response
                   */
                goto err_rem_channel;
            }
            else if ((subType == SIR_MAC_MGMT_DISASSOC) ||
                    (subType == SIR_MAC_MGMT_DEAUTH))
            {
                /* During EAP failure or P2P Group Remove supplicant
                 * is sending del_station command to driver. From
                 * del_station function, Driver will send deauth frame to
                 * p2p client. No need to send disassoc frame from here.
                 * so Drop the frame here and send tx indication back to
                 * supplicant.
                 */
                tANI_U8 dstMac[ETH_ALEN] = {0};
                memcpy(&dstMac, &buf[WLAN_HDD_80211_FRM_DA_OFFSET], ETH_ALEN);
                hddLog(VOS_TRACE_LEVEL_INFO,
                        "%s: Deauth/Disassoc received for STA:"
                        MAC_ADDRESS_STR,
                        __func__,
                        MAC_ADDR_ARRAY(dstMac));
                goto err_rem_channel;
            }
        }
    }

    if( NULL != cfgState->buf )
    {
        if ( !noack )
        {
            hddLog( LOGE, "(%s):Previous P2P Action frame packet pending",
                          __func__);
            hdd_cleanup_actionframe(pAdapter->pHddCtx, pAdapter);
        }
        else
        {
            hddLog( LOGE, "(%s):Pending Action frame packet return EBUSY",
                          __func__);
            return -EBUSY;
        }
    }

    if( subType == SIR_MAC_MGMT_ACTION)
    {
         hddLog( LOG1, "Action frame tx request : %s",
         hdd_getActionString(buf[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET]));
     }

    if ((pAdapter->device_mode == WLAN_HDD_SOFTAP) &&
        (test_bit(SOFTAP_BSS_STARTED, &pAdapter->event_flags))) {
        home_ch = pAdapter->sessionCtx.ap.operatingChannel;
    } else if ((pAdapter->device_mode == WLAN_HDD_INFRA_STATION) &&
               (pAdapter->sessionCtx.station.conn_info.connState ==
                        eConnectionState_Associated)) {
        home_ch = pAdapter->sessionCtx.station.conn_info.operationChannel;
    } else {
        goAdapter = hdd_get_adapter( pAdapter->pHddCtx, WLAN_HDD_P2P_GO );
        if (goAdapter &&
            (test_bit(SOFTAP_BSS_STARTED, &goAdapter->event_flags)))
            home_ch = goAdapter->sessionCtx.ap.operatingChannel;
    }


    if (ieee80211_frequency_to_channel(chan->center_freq) == home_ch) {
        /* if adapter is already on requested ch, no need for ROC */
        wait = 0;
        hddLog(LOGE,
               FL("Adapter already on requested channel. No ROC requested"));
        goto send_frame;
    }

    if( offchan && wait)
    {
        int status;
        rem_on_channel_request_type_t req_type = OFF_CHANNEL_ACTION_TX;
        // In case of P2P Client mode if we are already
        // on the same channel then send the frame directly

        mutex_lock(&cfgState->remain_on_chan_ctx_lock);
        pRemainChanCtx = cfgState->remain_on_chan_ctx;
        if ((type == SIR_MAC_MGMT_FRAME) &&
              (subType == SIR_MAC_MGMT_ACTION) &&
               hdd_p2p_is_action_type_rsp(
                &buf[WLAN_HDD_PUBLIC_ACTION_FRAME_BODY_OFFSET],
                len - mgmt_hdr_len) &&
               cfgState->remain_on_chan_ctx &&
               cfgState->current_freq == chan->center_freq )
         {
               if(VOS_TIMER_STATE_RUNNING == vos_timer_getCurrentState(
                   &cfgState->remain_on_chan_ctx->hdd_remain_on_chan_timer))
               {
                   /* In the latest wpa_supplicant, the wait time for go
                    * negotiation response is set to 100msec, due to which,
                    * there could be a possibility that, if the go negotaition
                    * confirmation frame is not received within 100 msec, ROC
                    * would be timeout and resulting in connection failures as
                    * the device will not be on the listen channel anymore to
                    * receive the confirmation frame.
                    * Also wpa_supplicant has set the wait to 50msec for go
                    * negotiation confirmation, invitation response and
                    * provisional discovery response frames. So increase the
                    * wait time for all these frames.
                    */
                   actionFrmType =
                          buf[WLAN_HDD_PUBLIC_ACTION_FRAME_TYPE_OFFSET];
                   if ( actionFrmType == WLAN_HDD_GO_NEG_RESP ||
                           actionFrmType == WLAN_HDD_PROV_DIS_RESP)
                       wait = wait + ACTION_FRAME_RSP_WAIT;
                   else if ( actionFrmType == WLAN_HDD_GO_NEG_CNF ||
                           actionFrmType == WLAN_HDD_INVITATION_RESP )
                       wait = wait + ACTION_FRAME_ACK_WAIT;

                   hddLog( LOG1, "%s: Extending the wait time %d for actionFrmType=%d",
                           __func__,wait,actionFrmType);

                   if (!VOS_IS_STATUS_SUCCESS(
                               vos_timer_stop(&cfgState->remain_on_chan_ctx->
                                   hdd_remain_on_chan_timer)))
                       hddLog( LOGE, FL("Failed to stop hdd_remain_on_chan_timer"));
                   status = vos_timer_start(&cfgState->remain_on_chan_ctx->hdd_remain_on_chan_timer,
                                                        wait);
                   mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
                   if(status != VOS_STATUS_SUCCESS)
                   {
                      hddLog( LOGE, "%s: Remain on Channel timer start failed",
                                    __func__);
                   }
                   goto send_frame;
               } else {

                  if( (pRemainChanCtx != NULL) &&
                      (pRemainChanCtx->hdd_remain_on_chan_cancel_in_progress ==
                           TRUE))
                  {
                      mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
                      hddLog(VOS_TRACE_LEVEL_INFO,
                          "action frame tx: waiting for completion of ROC ");

                      rc = wait_for_completion_timeout(
                          &pAdapter->cancel_rem_on_chan_var,
                          msecs_to_jiffies(WAIT_CANCEL_REM_CHAN));
                      if (!rc) {
                          hddLog( LOGE,
                              "%s:wait on cancel_rem_on_chan_var timed out",
                              __func__);
                      }

                  } else
                      mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
               }
         } else
             mutex_unlock(&cfgState->remain_on_chan_ctx_lock);

        mutex_lock(&cfgState->remain_on_chan_ctx_lock);
        if((cfgState->remain_on_chan_ctx != NULL) &&
           (cfgState->current_freq == chan->center_freq)
          )
        {
            mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
            hddLog(LOG1,"action frame: extending the wait time");
            extendedWait = (tANI_U16)wait;
            goto send_frame;
        }
        mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
        INIT_COMPLETION(pAdapter->offchannel_tx_event);
        status = wlan_hdd_request_remain_on_channel(wiphy, dev,
                                        chan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                                        channel_type,
#endif
                                        wait, cookie,
                                        req_type);
        if(0 != status)
        {
            if( (-EBUSY == status) &&
                (cfgState->current_freq == chan->center_freq) )
            {
                goto send_frame;
            }
            goto err_rem_channel;
        }
        /* This will extend timer in LIM when sending Any action frame
         * It will cover remain on channel timer till next action frame
         * in rx direction.
         */
        extendedWait = (tANI_U16)wait;
        /* Wait for driver to be ready on the requested channel */
        rc = wait_for_completion_timeout(
                     &pAdapter->offchannel_tx_event,
                     msecs_to_jiffies(WAIT_CHANGE_CHANNEL_FOR_OFFCHANNEL_TX));
        if(!rc) {
           hddLog( LOGE, "wait on offchannel_tx_event timed out");
           goto err_rem_channel;
        }
    }
    else if ( offchan )
    {
        /* Check before sending action frame
           whether we already remain on channel */
        if(NULL == cfgState->remain_on_chan_ctx)
        {
            goto err_rem_channel;
        }
    }
    send_frame:

    if(!noack)
    {
        cfgState->buf = vos_mem_malloc( len ); //buf;
        if( cfgState->buf == NULL )
            return -ENOMEM;

        cfgState->len = len;

        vos_mem_copy( cfgState->buf, buf, len);

        mutex_lock(&cfgState->remain_on_chan_ctx_lock);
       if( cfgState->remain_on_chan_ctx )
       {
          cfgState->action_cookie = cfgState->remain_on_chan_ctx->cookie;
          *cookie = cfgState->action_cookie;
       }
       else
       {
          *cookie = (uintptr_t) cfgState->buf;
          cfgState->action_cookie = *cookie;
       }
        mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
    }

    if ( (WLAN_HDD_INFRA_STATION == pAdapter->device_mode) ||
         (WLAN_HDD_P2P_CLIENT == pAdapter->device_mode) ||
         ( WLAN_HDD_P2P_DEVICE == pAdapter->device_mode )
       )
    {
        tANI_U8 sessionId = pAdapter->sessionId;

        if ((type == SIR_MAC_MGMT_FRAME) &&
                (subType == SIR_MAC_MGMT_ACTION) &&
                wlan_hdd_is_type_p2p_action(
                 &buf[WLAN_HDD_PUBLIC_ACTION_FRAME_BODY_OFFSET],
                 len - mgmt_hdr_len))
        {
            actionFrmType = buf[WLAN_HDD_PUBLIC_ACTION_FRAME_TYPE_OFFSET];
            hddLog(LOG1, "Tx Action Frame %u", actionFrmType);
            if (actionFrmType == WLAN_HDD_PROV_DIS_REQ)
            {
                cfgState->actionFrmState = HDD_PD_REQ_ACK_PENDING;
                hddLog(LOG1, "%s: HDD_PD_REQ_ACK_PENDING", __func__);
            }
            else if (actionFrmType == WLAN_HDD_GO_NEG_REQ)
            {
                cfgState->actionFrmState = HDD_GO_NEG_REQ_ACK_PENDING;
                hddLog(LOG1, "%s: HDD_GO_NEG_REQ_ACK_PENDING", __func__);
            }
        }

        if (!vos_mem_compare((uint8_t *)(&buf[WLAN_HDD_80211_FRM_SA_OFFSET]),
                        &pAdapter->macAddressCurrent, VOS_MAC_ADDR_SIZE)) {
            hddLog(LOG1, "%s: sa of action frame is randomized with mac-addr: "
               MAC_ADDRESS_STR, __func__,
               MAC_ADDR_ARRAY((uint8_t *)(&buf[WLAN_HDD_80211_FRM_SA_OFFSET])));
            enb_random_mac = true;
        }

        if (enb_random_mac && !noack)
            hdd_set_action_frame_random_mac(pAdapter,
                                (uint8_t *)(&buf[WLAN_HDD_80211_FRM_SA_OFFSET]),
                                *cookie);

        if (eHAL_STATUS_SUCCESS !=
               sme_sendAction( WLAN_HDD_GET_HAL_CTX(pAdapter),
                               sessionId, buf, len, extendedWait, noack))
        {
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                     "%s: sme_sendAction returned fail", __func__);
            goto err;
        }
    }
    else if( ( WLAN_HDD_SOFTAP== pAdapter->device_mode ) ||
              ( WLAN_HDD_P2P_GO == pAdapter->device_mode )
            )
     {
        if( VOS_STATUS_SUCCESS !=
#ifdef WLAN_FEATURE_MBSSID
             WLANSAP_SendAction( WLAN_HDD_GET_SAP_CTX_PTR(pAdapter),
#else
             WLANSAP_SendAction( (WLAN_HDD_GET_CTX(pAdapter))->pvosContext,
#endif
                                  buf, len, 0 ) )

        {
            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                    "%s: WLANSAP_SendAction returned fail", __func__);
            goto err;
        }
    }

    return 0;
err:
    if(!noack)
    {
       if (enb_random_mac &&
           ((pAdapter->device_mode == WLAN_HDD_INFRA_STATION) ||
           (pAdapter->device_mode == WLAN_HDD_P2P_CLIENT) ||
           (pAdapter->device_mode == WLAN_HDD_P2P_DEVICE)))
           hdd_reset_action_frame_random_mac(pAdapter,
                                (uint8_t *)(&buf[WLAN_HDD_80211_FRM_SA_OFFSET]),
                                *cookie);
       hdd_sendActionCnf( pAdapter, FALSE );
    }
    return 0;
err_rem_channel:
    *cookie = (uintptr_t)cfgState;
    cfg80211_mgmt_tx_status(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                            pAdapter->dev->ieee80211_ptr,
#else
                            pAdapter->dev,
#endif
                            *cookie, buf, len, FALSE, GFP_KERNEL );
    EXIT();
    return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)) || defined(WITH_BACKPORTS)
int wlan_hdd_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
                     struct cfg80211_mgmt_tx_params *params, u64 *cookie)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
int wlan_hdd_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
                     struct ieee80211_channel *chan, bool offchan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                     enum nl80211_channel_type channel_type,
                     bool channel_type_valid,
#endif
                     unsigned int wait,
                     const u8 *buf, size_t len,  bool no_cck,
                     bool dont_wait_for_ack, u64 *cookie )
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
int wlan_hdd_mgmt_tx(struct wiphy *wiphy, struct net_device *dev,
                     struct ieee80211_channel *chan, bool offchan,
                     enum nl80211_channel_type channel_type,
                     bool channel_type_valid, unsigned int wait,
                     const u8 *buf, size_t len,  bool no_cck,
                     bool dont_wait_for_ack, u64 *cookie )
#else
int wlan_hdd_mgmt_tx(struct wiphy *wiphy, struct net_device *dev,
                     struct ieee80211_channel *chan, bool offchan,
                     enum nl80211_channel_type channel_type,
                     bool channel_type_valid, unsigned int wait,
                     const u8 *buf, size_t len, u64 *cookie )
#endif /* LINUX_VERSION_CODE */
{
    int ret;

    vos_ssr_protect(__func__);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,14,0)) || defined(WITH_BACKPORTS)
    ret = __wlan_hdd_mgmt_tx(wiphy, wdev, params->chan, params->offchan,
                             params->wait, params->buf, params->len,
                             params->no_cck, params->dont_wait_for_ack,
                             cookie);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
    ret = __wlan_hdd_mgmt_tx(wiphy, wdev, chan, offchan,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,8,0)) && !defined(WITH_BACKPORTS)
                             channel_type, channel_type_valid,
#endif
                             wait, buf, len, no_cck,
                             dont_wait_for_ack, cookie);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,3,0))
    ret = __wlan_hdd_mgmt_tx(wiphy, dev, chan, offchan,
                             channel_type, channel_type_valid, wait,
                             buf, len, no_cck, dont_wait_for_ack, cookie);
#else
    ret = __wlan_hdd_mgmt_tx(wiphy, dev, chan, offchan,
                             channel_type, channel_type_valid, wait,
                             buf, len, cookie);
#endif /* LINUX_VERSION_CODE */
    vos_ssr_unprotect(__func__);

    return ret;
}

/**
 * hdd_wlan_delete_mgmt_tx_cookie() - Wrapper to delete action frame cookie
 * @wdev: Pointer to wireless device
 * @cookie: Cookie to be deleted
 *
 * This is a wrapper function which actually invokes the hdd api to delete
 * cookie based on the device mode of adapter.
 *
 * Return: 0 - for success else negative value
 */
static int hdd_wlan_delete_mgmt_tx_cookie(struct wireless_dev *wdev,
				   u64 cookie)
{
	struct net_device *dev = wdev->netdev;
	hdd_adapter_t *adapter = WLAN_HDD_GET_PRIV_PTR(dev);

	if ((adapter->device_mode == WLAN_HDD_INFRA_STATION) ||
	    (adapter->device_mode == WLAN_HDD_P2P_CLIENT) ||
	    (adapter->device_mode == WLAN_HDD_P2P_DEVICE)) {
		hdd_delete_action_frame_cookie(adapter, cookie);
	}

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
int __wlan_hdd_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
                                            struct wireless_dev *wdev,
                                            u64 cookie)
{
    if (VOS_FTM_MODE == hdd_get_conparam()) {
        hddLog(LOGE, FL("Command not allowed in FTM mode"));
        return -EINVAL;
    }

    hdd_wlan_delete_mgmt_tx_cookie(wdev, cookie);

    return wlan_hdd_cfg80211_cancel_remain_on_channel(wiphy, wdev, cookie);
}
#else
int __wlan_hdd_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
                                            struct net_device *dev,
                                            u64 cookie)
{
    return wlan_hdd_cfg80211_cancel_remain_on_channel(wiphy, dev, cookie);
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
int wlan_hdd_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
                                          struct wireless_dev *wdev,
                                          u64 cookie)
{
    int ret;

    vos_ssr_protect(__func__);
    ret = __wlan_hdd_cfg80211_mgmt_tx_cancel_wait(wiphy, wdev, cookie);
    vos_ssr_unprotect(__func__);

    return ret;
}
#else
int wlan_hdd_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
                                          struct net_device *dev,
                                          u64 cookie)
{
    int ret;

    vos_ssr_protect(__func__);
    ret = __wlan_hdd_cfg80211_mgmt_tx_cancel_wait(wiphy, dev, cookie);
    vos_ssr_unprotect(__func__);

    return ret;
}
#endif

void hdd_sendActionCnf( hdd_adapter_t *pAdapter, tANI_BOOLEAN actionSendSuccess )
{
    hdd_cfg80211_state_t *cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );

    cfgState->actionFrmState = HDD_IDLE;

    if( NULL == cfgState->buf )
    {
        return;
    }
    if (cfgState->is_go_neg_ack_received) {

        cfgState->is_go_neg_ack_received = 0 ;
        /* Sometimes its possible that host may receive the ack for GO
         * negotiation req after sending go negotaition confirmation,
         * in such case drop the ack received for the go negotiation
         * request, so that supplicant waits for the confirmation ack
         * from firmware.
         */
        hddLog( LOG1, FL("Drop the pending ack received in cfgState->actionFrmState %d"),
                cfgState->actionFrmState);
        return;
    }

    hddLog( LOG1, "Send Action cnf, actionSendSuccess %d", actionSendSuccess);

    /*
     * buf is the same pointer it passed us to send. Since we are sending
     * it through control path, we use different buffers.
     * In case of mac80211, they just push it to the skb and pass the same
     * data while sending tx ack status.
     * */
    cfg80211_mgmt_tx_status(
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
                pAdapter->dev->ieee80211_ptr,
#else
                pAdapter->dev,
#endif
                cfgState->action_cookie,
                cfgState->buf, cfgState->len, actionSendSuccess, GFP_KERNEL );
    vos_mem_free(cfgState->buf);
    cfgState->buf = NULL;

    complete(&pAdapter->tx_action_cnf_event);
}

/**
 * hdd_send_action_cnf_cb - action confirmation callback
 * @session_id: SME session ID
 * @tx_completed: ack status
 *
 * This function invokes hdd_sendActionCnf to update ack status to
 * supplicant.
 */
void hdd_send_action_cnf_cb(uint32_t session_id, bool tx_completed)
{
	v_CONTEXT_t vos_context;
	hdd_context_t *hdd_ctx;
	hdd_adapter_t *adapter;

	ENTER();

	/* Get the global VOSS context */
	vos_context = vos_get_global_context(VOS_MODULE_ID_SYS, NULL);
	if (!vos_context) {
		hddLog(LOGE, FL("Global VOS context is Null"));
		return;
	}

	/* Get the HDD context.*/
	hdd_ctx = vos_get_context(VOS_MODULE_ID_HDD, vos_context);
	if (0 != wlan_hdd_validate_context(hdd_ctx))
		return;

	adapter = hdd_get_adapter_by_sme_session_id(hdd_ctx, session_id);
	if (NULL == adapter) {
		hddLog(LOGE, FL("adapter not found"));
		return;
	}

	if (WLAN_HDD_ADAPTER_MAGIC != adapter->magic) {
		hddLog(LOGE, FL("adapter has invalid magic"));
		return;
	}

	hdd_sendActionCnf(adapter, tx_completed) ;
}

/**
 * hdd_setP2pNoa
 *
 *FUNCTION:
 * This function is called from hdd_hostapd_ioctl function when Driver
 * get P2P_SET_NOA command from wpa_supplicant using private ioctl
 *
 *LOGIC:
 * Fill NoA Struct According to P2P Power save Option and Pass it to SME layer
 *
 *ASSUMPTIONS:
 *
 *
 *NOTE:
 *
 * @param dev          Pointer to net device structure
 * @param command      Pointer to command
 *
 * @return Status
 */

int hdd_setP2pNoa( struct net_device *dev, tANI_U8 *command )
{
    hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    tHalHandle hHal = WLAN_HDD_GET_HAL_CTX(pAdapter);
    VOS_STATUS status = VOS_STATUS_SUCCESS;
    tP2pPsConfig NoA;
    int count, duration, start_time;
    char *param;
    int ret;

    param = strnchr(command, strlen(command), ' ');
    if (param == NULL)
    {
       VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
              "%s: strnchr failed to find delimeter", __func__);
       return -EINVAL;
    }
    param++;
    ret = sscanf(param, "%d %d %d", &count, &start_time, &duration);
    if (ret != 3) {
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
               "%s: P2P_SET GO NoA: fail to read params, ret=%d",
                __func__, ret);
        return -EINVAL;
    }
    VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
               "%s: P2P_SET GO NoA: count=%d start_time=%d duration=%d",
                __func__, count, start_time, duration);
    duration = MS_TO_MUS(duration);
    /* PS Selection
     * Periodic NoA (2)
     * Single NOA   (4)
     */
    NoA.opp_ps = 0;
    NoA.ctWindow = 0;
    if (count == 1)
    {
        NoA.duration = 0;
        NoA.single_noa_duration = duration;
        NoA.psSelection = P2P_POWER_SAVE_TYPE_SINGLE_NOA;
    }
    else
    {
        NoA.duration = duration;
        NoA.single_noa_duration = 0;
        NoA.psSelection = P2P_POWER_SAVE_TYPE_PERIODIC_NOA;
    }
    /* NOA interval in TU */
    NoA.interval = NOA_INTERVAL_IN_TU;
    NoA.count = count;
    NoA.sessionid = pAdapter->sessionId;

    VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                "%s: P2P_PS_ATTR:oppPS %d ctWindow %d duration %d "
                "interval %d count %d single noa duration %d "
                "PsSelection %x", __func__, NoA.opp_ps,
                NoA.ctWindow, NoA.duration, NoA.interval,
                NoA.count, NoA.single_noa_duration,
                NoA.psSelection);

    sme_p2pSetPs(hHal, &NoA);
    return status;
}

/**
 * hdd_setP2pOpps
 *
 *FUNCTION:
 * This function is called from hdd_hostapd_ioctl function when Driver
 * get P2P_SET_PS command from wpa_supplicant using private ioctl
 *
 *LOGIC:
 * Fill NoA Struct According to P2P Power save Option and Pass it to SME layer
 *
 *ASSUMPTIONS:
 *
 *
 *NOTE:
 *
 * @param  dev         Pointer to net device structure
 * @param  command     Pointer to command
 *
 * @return Status
 */

int hdd_setP2pOpps( struct net_device *dev, tANI_U8 *command )
{
    hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    tHalHandle hHal = WLAN_HDD_GET_HAL_CTX(pAdapter);
    VOS_STATUS status = VOS_STATUS_SUCCESS;
    tP2pPsConfig NoA;
    char *param;
    int legacy_ps, opp_ps, ctwindow;
    int ret;

    param = strnchr(command, strlen(command), ' ');
    if (param == NULL)
    {
        VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                  "%s: strnchr failed to find delimiter", __func__);
        return -EINVAL;
    }
    param++;
    ret = sscanf(param, "%d %d %d", &legacy_ps, &opp_ps, &ctwindow);
    if (ret != 3) {
        VOS_TRACE (VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_ERROR,
                 "%s: P2P_SET GO PS: fail to read params, ret=%d",
                 __func__, ret);
        return -EINVAL;
    }
    VOS_TRACE (VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                 "%s: P2P_SET GO PS: legacy_ps=%d opp_ps=%d ctwindow=%d",
                 __func__, legacy_ps, opp_ps, ctwindow);

    /* PS Selection
     * Opportunistic Power Save (1)
     */

    /* From wpa_cli user need to use separate command to set ctWindow and Opps
     * When user want to set ctWindow during that time other parameters
     * values are coming from wpa_supplicant as -1.
     * Example : User want to set ctWindow with 30 then wpa_cli command :
     * P2P_SET ctwindow 30
     * Command Received at hdd_hostapd_ioctl is as below:
     * P2P_SET_PS -1 -1 30 (legacy_ps = -1, opp_ps = -1, ctwindow = 30)
     */
    if (ctwindow != -1)
    {

        VOS_TRACE(VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                    "Opportunistic Power Save is %s",
                    (TRUE == pAdapter->ops) ? "Enable" : "Disable" );

        if (ctwindow != pAdapter->ctw)
        {
            pAdapter->ctw = ctwindow;

            if(pAdapter->ops)
            {
                NoA.opp_ps = pAdapter->ops;
                NoA.ctWindow = pAdapter->ctw;
                NoA.duration = 0;
                NoA.single_noa_duration = 0;
                NoA.interval = 0;
                NoA.count = 0;
                NoA.psSelection = P2P_POWER_SAVE_TYPE_OPPORTUNISTIC;
                NoA.sessionid = pAdapter->sessionId;

                VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                            "%s: P2P_PS_ATTR:oppPS %d ctWindow %d duration %d "
                            "interval %d count %d single noa duration %d "
                            "PsSelection %x", __func__, NoA.opp_ps,
                            NoA.ctWindow, NoA.duration, NoA.interval,
                            NoA.count, NoA.single_noa_duration,
                            NoA.psSelection);

               sme_p2pSetPs(hHal, &NoA);
           }
           return 0;
        }
    }

    if (opp_ps != -1)
    {
        pAdapter->ops = opp_ps;


        if ((opp_ps != -1) && (pAdapter->ctw))
        {
            NoA.opp_ps = opp_ps;
            NoA.ctWindow = pAdapter->ctw;
            NoA.duration = 0;
            NoA.single_noa_duration = 0;
            NoA.interval = 0;
            NoA.count = 0;
            NoA.psSelection = P2P_POWER_SAVE_TYPE_OPPORTUNISTIC;
            NoA.sessionid = pAdapter->sessionId;

            VOS_TRACE( VOS_MODULE_ID_HDD, VOS_TRACE_LEVEL_INFO,
                        "%s: P2P_PS_ATTR:oppPS %d ctWindow %d duration %d "
                        "interval %d count %d single noa duration %d "
                        "PsSelection %x", __func__, NoA.opp_ps,
                        NoA.ctWindow, NoA.duration, NoA.interval,
                        NoA.count, NoA.single_noa_duration,
                        NoA.psSelection);

           sme_p2pSetPs(hHal, &NoA);
        }
    }
    return status;
}

int hdd_setP2pPs( struct net_device *dev, void *msgData )
{
    hdd_adapter_t *pAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    tHalHandle hHal = WLAN_HDD_GET_HAL_CTX(pAdapter);
    VOS_STATUS status = VOS_STATUS_SUCCESS;
    tP2pPsConfig NoA;
    p2p_app_setP2pPs_t *pappNoA = (p2p_app_setP2pPs_t *) msgData;

    NoA.opp_ps = pappNoA->opp_ps;
    NoA.ctWindow = pappNoA->ctWindow;
    NoA.duration = pappNoA->duration;
    NoA.interval = pappNoA->interval;
    NoA.count = pappNoA->count;
    NoA.single_noa_duration = pappNoA->single_noa_duration;
    NoA.psSelection = pappNoA->psSelection;
    NoA.sessionid = pAdapter->sessionId;

    sme_p2pSetPs(hHal, &NoA);
    return status;
}

static tANI_U8 wlan_hdd_get_session_type( enum nl80211_iftype type )
{
    tANI_U8 sessionType;

    switch( type )
    {
        case NL80211_IFTYPE_AP:
            sessionType = WLAN_HDD_SOFTAP;
            break;
        case NL80211_IFTYPE_P2P_GO:
            sessionType = WLAN_HDD_P2P_GO;
            break;
        case NL80211_IFTYPE_P2P_CLIENT:
            sessionType = WLAN_HDD_P2P_CLIENT;
            break;
        case NL80211_IFTYPE_STATION:
            sessionType = WLAN_HDD_INFRA_STATION;
            break;
        case NL80211_IFTYPE_MONITOR:
            sessionType = WLAN_HDD_MONITOR;
            break;
        default:
            sessionType = WLAN_HDD_INFRA_STATION;
            break;
    }

    return sessionType;
}

struct wireless_dev* __wlan_hdd_add_virtual_intf(
                  struct wiphy *wiphy, const char *name,
                  unsigned char name_assign_type,
                  enum nl80211_iftype type,
                  u32 *flags, struct vif_params *params )
{
    hdd_context_t *pHddCtx = (hdd_context_t*) wiphy_priv(wiphy);
    hdd_adapter_t* pAdapter = NULL;
    hdd_scaninfo_t *scan_info = NULL;
    int ret;

    ENTER();

    ret = wlan_hdd_validate_context(pHddCtx);
    if (0 != ret)
        return ERR_PTR(ret);

    if (VOS_FTM_MODE == hdd_get_conparam()) {
        hddLog(LOGE, FL("Command not allowed in FTM mode"));
        return ERR_PTR(-EINVAL);
    }

    MTRACE(vos_trace(VOS_MODULE_ID_HDD,
                     TRACE_CODE_HDD_ADD_VIRTUAL_INTF, NO_SESSION, type));
    /*Allow addition multiple interface for WLAN_HDD_P2P_CLIENT and
      WLAN_HDD_SOFTAP session type*/
    if ((hdd_get_adapter(pHddCtx, wlan_hdd_get_session_type(type)) != NULL)
#ifdef WLAN_FEATURE_MBSSID
        && WLAN_HDD_SOFTAP != wlan_hdd_get_session_type(type)
#endif
        && WLAN_HDD_P2P_CLIENT != wlan_hdd_get_session_type(type)
        && WLAN_HDD_INFRA_STATION != wlan_hdd_get_session_type(type)
            )
    {
       hddLog(VOS_TRACE_LEVEL_ERROR,"%s: Interface type %d already exists. "
                  "Two interfaces of same type are not supported currently.",
                  __func__, type);
       return ERR_PTR(-EINVAL);
    }

    pAdapter = hdd_get_adapter(pHddCtx, WLAN_HDD_INFRA_STATION);
    if (pAdapter != NULL) {
        scan_info = &pAdapter->scan_info;
        if ((scan_info != NULL) && (scan_info->mScanPending)) {
            hdd_abort_mac_scan(pHddCtx, pAdapter->sessionId,
                           eCSR_SCAN_ABORT_DEFAULT);
            hddLog(LOG1, FL("Abort Scan while adding virtual interface"));
        }
    }

    pAdapter = NULL;

    wlan_hdd_tdls_disable_offchan_and_teardown_links(pHddCtx);

    if (pHddCtx->cfg_ini->isP2pDeviceAddrAdministrated &&
        ((NL80211_IFTYPE_P2P_GO == type) ||
         (NL80211_IFTYPE_P2P_CLIENT == type)))
    {
            /* Generate the P2P Interface Address. this address must be
             * different from the P2P Device Address.
             */
            v_MACADDR_t p2pDeviceAddress = pHddCtx->p2pDeviceAddress;
            p2pDeviceAddress.bytes[4] ^= 0x80;
            pAdapter = hdd_open_adapter( pHddCtx,
                                         wlan_hdd_get_session_type(type),
                                         name, p2pDeviceAddress.bytes,
                                         name_assign_type,
                                         VOS_TRUE );
            if (WLAN_HDD_RX_HANDLE_RPS == pHddCtx->cfg_ini->rxhandle)
                hdd_dp_util_send_rps_ind(pAdapter);
    }
    else
    {
       pAdapter = hdd_open_adapter( pHddCtx, wlan_hdd_get_session_type(type),
                          name, wlan_hdd_get_intf_addr(pHddCtx),
                          name_assign_type,
                          VOS_TRUE);
       if (WLAN_HDD_RX_HANDLE_RPS == pHddCtx->cfg_ini->rxhandle)
           hdd_dp_util_send_rps_ind(pAdapter);
    }

    if( NULL == pAdapter)
    {
        hddLog(VOS_TRACE_LEVEL_ERROR,"%s: hdd_open_adapter failed",__func__);
        return ERR_PTR(-ENOSPC);
    }
#ifdef WLAN_FEATURE_SAP_TO_FOLLOW_STA_CHAN
    if(pAdapter->device_mode == WLAN_HDD_SOFTAP)
    {
	    if(pHddCtx->ch_switch_ctx.chan_sw_timer_initialized == VOS_FALSE)
	    {
		    //Initialize the channel switch timer
		    ret = vos_timer_init(&pHddCtx->ch_switch_ctx.hdd_ap_chan_switch_timer, VOS_TIMER_TYPE_SW,
				    hdd_hostapd_chan_switch_cb, (v_PVOID_t)pAdapter);
		    if(!VOS_IS_STATUS_SUCCESS(ret))
		    {
			    hddLog(LOGE, FL("Failed to initialize AP channel switch timer!!\n"));
			    EXIT();
			    return ERR_PTR(ret);
		    }
		    pHddCtx->ch_switch_ctx.chan_sw_timer_initialized = VOS_TRUE;
	    }
    }
#endif //WLAN_FEATURE_SAP_TO_FOLLOW_STA_CHAN

    EXIT();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
    return pAdapter->dev->ieee80211_ptr;
#else
    return pAdapter->dev;
#endif
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
struct wireless_dev *wlan_hdd_add_virtual_intf(struct wiphy *wiphy,
					       const char *name,
					       unsigned char name_assign_type,
					       enum nl80211_iftype type,
					       struct vif_params *params)
{
	struct wireless_dev *wdev;

	vos_ssr_protect(__func__);
	wdev = __wlan_hdd_add_virtual_intf(wiphy, name, name_assign_type,
					   type, &params->flags, params);
	vos_ssr_unprotect(__func__);

	return wdev;
}
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0))
/**
 * wlan_hdd_add_virtual_intf() - Add virtual interface wrapper
 * @wiphy: wiphy pointer
 * @name: User-visible name of the interface
 * @name_assign_type: the name of assign type of the netdev
 * @nl80211_iftype: (virtual) interface types
 * @flags: monitor mode configuration flags (not used)
 * @vif_params: virtual interface parameters (not used)
 *
 * Return: the pointer of wireless dev, otherwise NULL.
 */
struct wireless_dev *wlan_hdd_add_virtual_intf(struct wiphy *wiphy,
                                               const char *name,
                                               unsigned char name_assign_type,
                                               enum nl80211_iftype type,
                                               u32 *flags,
                                               struct vif_params *params)
{
    struct wireless_dev *wdev;

    vos_ssr_protect(__func__);
    wdev = __wlan_hdd_add_virtual_intf(wiphy, name, name_assign_type,
                                       type, flags, params);
    vos_ssr_unprotect(__func__);
    return wdev;
}
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) || defined(WITH_BACKPORTS)
struct wireless_dev* wlan_hdd_add_virtual_intf(
                  struct wiphy *wiphy, const char *name,
                  enum nl80211_iftype type,
                  u32 *flags, struct vif_params *params )
{
    struct wireless_dev* wdev;
    unsigned char name_assign_type = 0;

    vos_ssr_protect(__func__);
    wdev = __wlan_hdd_add_virtual_intf(wiphy, name, name_assign_type,
                                       type, flags, params);
    vos_ssr_unprotect(__func__);
    return wdev;
}
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
struct wireless_dev* wlan_hdd_add_virtual_intf(
                  struct wiphy *wiphy, char *name, enum nl80211_iftype type,
                  u32 *flags, struct vif_params *params )
{
    struct wireless_dev* wdev;
    unsigned char name_assign_type = 0;

    vos_ssr_protect(__func__);
    wdev = __wlan_hdd_add_virtual_intf(wiphy, name, name_assign_type,
                                       type, flags, params);
    vos_ssr_unprotect(__func__);
    return wdev;
}
#else
struct net_device* wlan_hdd_add_virtual_intf(
                  struct wiphy *wiphy, char *name, enum nl80211_iftype type,
                  u32 *flags, struct vif_params *params )
{
    struct net_device* ndev;
    unsigned char name_assign_type = 0;

    vos_ssr_protect(__func__);
    ndev = __wlan_hdd_add_virtual_intf(wiphy, name, name_assign_type,
                                       type, flags, params);
    vos_ssr_unprotect(__func__);
    return ndev;
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
int __wlan_hdd_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
#else
int __wlan_hdd_del_virtual_intf(struct wiphy *wiphy, struct net_device *dev)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
    struct net_device *dev = wdev->netdev;
#endif
    hdd_context_t *pHddCtx = (hdd_context_t*) wiphy_priv(wiphy);
    hdd_adapter_t *pVirtAdapter = WLAN_HDD_GET_PRIV_PTR(dev);
    int status;
    ENTER();

    MTRACE(vos_trace(VOS_MODULE_ID_HDD,
                     TRACE_CODE_HDD_DEL_VIRTUAL_INTF,
                     pVirtAdapter->sessionId, pVirtAdapter->device_mode));
    hddLog(LOG1, FL("Device_mode %s(%d)"),
           hdd_device_mode_to_string(pVirtAdapter->device_mode),
           pVirtAdapter->device_mode);

    status = wlan_hdd_validate_context(pHddCtx);
    if (0 != status)
        return status;

    if (VOS_FTM_MODE == hdd_get_conparam()) {
        hddLog(LOGE, FL("Command not allowed in FTM mode"));
        return -EINVAL;
    }

    wlan_hdd_release_intf_addr( pHddCtx,
                                 pVirtAdapter->macAddressCurrent.bytes );

    hdd_stop_adapter( pHddCtx, pVirtAdapter, VOS_TRUE );
    hdd_close_adapter( pHddCtx, pVirtAdapter, TRUE );
    EXIT();
    return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
int wlan_hdd_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
#else
int wlan_hdd_del_virtual_intf(struct wiphy *wiphy, struct net_device *dev)
#endif
{
    int ret;

    vos_ssr_protect(__func__);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)) || defined(WITH_BACKPORTS)
    ret = __wlan_hdd_del_virtual_intf(wiphy, wdev);
#else
    ret = __wlan_hdd_del_virtual_intf(wiphy, dev);
#endif
    vos_ssr_unprotect(__func__);

    return ret;
}

#if defined(WLAN_FEATURE_SAE) && defined(CFG80211_EXTERNAL_AUTH_AP_SUPPORT)
/**
 * wlan_hdd_set_rxmgmt_external_auth_flag() - Set the EXTERNAL_AUTH flag
 * @nl80211_flag: flags to be sent to nl80211 from enum nl80211_rxmgmt_flags
 *
 * Set the flag NL80211_RXMGMT_FLAG_EXTERNAL_AUTH if supported.
 */
static void
wlan_hdd_set_rxmgmt_external_auth_flag(enum nl80211_rxmgmt_flags *nl80211_flag)
{
	*nl80211_flag |= NL80211_RXMGMT_FLAG_EXTERNAL_AUTH;
}
#else
static void
wlan_hdd_set_rxmgmt_external_auth_flag(enum nl80211_rxmgmt_flags *nl80211_flag)
{
}
#endif

/**
 * wlan_hdd_cfg80211_convert_rxmgmt_flags() - Convert RXMGMT value
 * @nl80211_flag: Flags to be sent to nl80211 from enum nl80211_rxmgmt_flags
 * @flag: flags set by driver(SME/PE) from enum rxmgmt_flags
 *
 * Convert driver internal RXMGMT flag value to nl80211 defined RXMGMT flag
 * Return: 0 on success, -EINVAL on invalid value
 */
static int
wlan_hdd_cfg80211_convert_rxmgmt_flags(enum rxmgmt_flags flag,
				       enum nl80211_rxmgmt_flags *nl80211_flag)
{
	int ret = -EINVAL;

	if (flag & RXMGMT_FLAG_EXTERNAL_AUTH) {
		wlan_hdd_set_rxmgmt_external_auth_flag(nl80211_flag);
		ret = 0;
	}

	return ret;
}

void __hdd_indicate_mgmt_frame(hdd_adapter_t *pAdapter,
                            tANI_U32 nFrameLength,
                            tANI_U8* pbFrames,
                            tANI_U8 frameType,
                            tANI_U32 rxChan,
                            tANI_S8 rxRssi,
                            enum rxmgmt_flags rx_flags)
{
    tANI_U16 freq;
    tANI_U16 extend_time;
    tANI_U8 type = 0;
    tANI_U8 subType = 0;
    tActionFrmType actionFrmType;
    hdd_cfg80211_state_t *cfgState = NULL;
    VOS_STATUS status;
    hdd_remain_on_chan_ctx_t* pRemainChanCtx = NULL;
    hdd_context_t *pHddCtx;
    uint8_t broadcast = 0;
    enum nl80211_rxmgmt_flags nl80211_flag = 0;

    hddLog(VOS_TRACE_LEVEL_INFO, FL("Frame Type = %d Frame Length = %d"),
            frameType, nFrameLength);

    if (NULL == pAdapter)
    {
        hddLog(LOGE, FL("pAdapter is NULL"));
        return;
    }
    pHddCtx = WLAN_HDD_GET_CTX(pAdapter);

    if (0 == nFrameLength)
    {
        hddLog(LOGE, FL("Frame Length is Invalid ZERO"));
        return;
    }

    if (NULL == pbFrames)
    {
        hddLog(LOGE, FL("pbFrames is NULL"));
        return;
    }

    type = WLAN_HDD_GET_TYPE_FRM_FC(pbFrames[0]);
    subType = WLAN_HDD_GET_SUBTYPE_FRM_FC(pbFrames[0]);

    /* Get pAdapter from Destination mac address of the frame */
    if ((type == SIR_MAC_MGMT_FRAME) &&
        (subType != SIR_MAC_MGMT_PROBE_REQ) &&
        (nFrameLength > WLAN_HDD_80211_FRM_DA_OFFSET + VOS_MAC_ADDR_SIZE) &&
        !vos_is_macaddr_broadcast(
         (v_MACADDR_t *)&pbFrames[WLAN_HDD_80211_FRM_DA_OFFSET]))
    {
         pAdapter = hdd_get_adapter_by_macaddr(pHddCtx,
                            &pbFrames[WLAN_HDD_80211_FRM_DA_OFFSET]);
         if (NULL == pAdapter)
         {
             pAdapter = hdd_get_adapter_by_rand_macaddr(pHddCtx,
                                     &pbFrames[WLAN_HDD_80211_FRM_DA_OFFSET]);
         }

         if (NULL == pAdapter)
         {
             /* We will receive broadcast management frames in OCB mode */
             pAdapter = hdd_get_adapter(pHddCtx, WLAN_HDD_OCB);
             if (NULL == pAdapter || !vos_is_macaddr_broadcast(
                     (v_MACADDR_t *)&pbFrames[WLAN_HDD_80211_FRM_DA_OFFSET]))
             {
                 /* Under assumption that we don't receive any action frame
                  * with BCST as destination we dropping action frame
                  */
                 hddLog(VOS_TRACE_LEVEL_FATAL,"pAdapter for action frame is NULL Macaddr = "
                                   MAC_ADDRESS_STR ,
                                   MAC_ADDR_ARRAY(&pbFrames[WLAN_HDD_80211_FRM_DA_OFFSET]));
                 hddLog(VOS_TRACE_LEVEL_FATAL, "%s: Frame Type = %d Frame Length = %d"
                                  " subType = %d", __func__, frameType,
                                  nFrameLength, subType);
                 return;
             }

             broadcast = 1;
         }
    }


    if (NULL == pAdapter->dev)
    {
        hddLog( LOGE, FL("pAdapter->dev is NULL"));
        return;
    }

    if (WLAN_HDD_ADAPTER_MAGIC != pAdapter->magic)
    {
        hddLog( LOGE, FL("pAdapter has invalid magic"));
        return;
    }

    //Channel indicated may be wrong. TODO
    //Indicate an action frame.
    if( rxChan <= MAX_NO_OF_2_4_CHANNELS )
    {
        freq = ieee80211_channel_to_frequency( rxChan,
                HDD_NL80211_BAND_2GHZ);
    }
    else
    {
        freq = ieee80211_channel_to_frequency( rxChan,
                HDD_NL80211_BAND_5GHZ);
    }

    cfgState = WLAN_HDD_GET_CFG_STATE_PTR( pAdapter );

    if ((type == SIR_MAC_MGMT_FRAME) &&
        (subType == SIR_MAC_MGMT_ACTION) && !broadcast &&
        (nFrameLength > WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET + 1))
    {
        if(pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET] == WLAN_HDD_PUBLIC_ACTION_FRAME)
        {
            // public action frame
            if((WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET + SIR_MAC_P2P_OUI_SIZE + 2 <
                nFrameLength) &&
               (pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET+1] ==
                SIR_MAC_ACTION_VENDOR_SPECIFIC) &&
                vos_mem_compare(&pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET+2], SIR_MAC_P2P_OUI, SIR_MAC_P2P_OUI_SIZE))
            // P2P action frames
            {
#ifdef WLAN_DEBUG
                u8 *macFrom = &pbFrames[WLAN_HDD_80211_FRM_DA_OFFSET+6];
#endif
                actionFrmType = pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_TYPE_OFFSET];
                hddLog(LOG1, "Rx Action Frame %u", actionFrmType);
#ifdef WLAN_FEATURE_P2P_DEBUG
                if(actionFrmType >= MAX_P2P_ACTION_FRAME_TYPE)
                {
                    hddLog(VOS_TRACE_LEVEL_ERROR,"[P2P] unknown[%d] <--- OTA"
                           " from " MAC_ADDRESS_STR, actionFrmType,
                           MAC_ADDR_ARRAY(macFrom));
                }
                else
                {
                    hddLog(VOS_TRACE_LEVEL_ERROR,"[P2P] %s <--- OTA"
                           " from " MAC_ADDRESS_STR,
                           p2p_action_frame_type[actionFrmType],
                           MAC_ADDR_ARRAY(macFrom));
                    if( (actionFrmType == WLAN_HDD_PROV_DIS_REQ) &&
                        (globalP2PConnectionStatus == P2P_NOT_ACTIVE) )
                    {
                         globalP2PConnectionStatus = P2P_GO_NEG_PROCESS;
                         hddLog(LOGE,"[P2P State]Inactive state to "
                           "GO negotiation progress state");
                    }
                    else if( (actionFrmType == WLAN_HDD_GO_NEG_CNF) &&
                        (globalP2PConnectionStatus == P2P_GO_NEG_PROCESS) )
                    {
                         globalP2PConnectionStatus = P2P_GO_NEG_COMPLETED;
                 hddLog(LOGE,"[P2P State]GO negotiation progress to "
                             "GO negotiation completed state");
                    }
                    else if( (actionFrmType == WLAN_HDD_INVITATION_REQ) &&
                        (globalP2PConnectionStatus == P2P_NOT_ACTIVE) )
                    {
                         globalP2PConnectionStatus = P2P_GO_NEG_COMPLETED;
                         hddLog(LOGE,"[P2P State]Inactive state to GO negotiation"
                                     " completed state Autonomous GO formation");
                    }
                }
#endif
            mutex_lock(&cfgState->remain_on_chan_ctx_lock);
            pRemainChanCtx = cfgState->remain_on_chan_ctx;
            if (pRemainChanCtx != NULL)
            {
                if(actionFrmType == WLAN_HDD_GO_NEG_REQ ||
                     actionFrmType == WLAN_HDD_GO_NEG_RESP ||
                     actionFrmType == WLAN_HDD_INVITATION_REQ ||
                     actionFrmType == WLAN_HDD_DEV_DIS_REQ ||
                     actionFrmType == WLAN_HDD_PROV_DIS_REQ )
                 {
                      hddLog( LOG1, "Extend RoC timer on reception of Action Frame");

                      if ((actionFrmType == WLAN_HDD_GO_NEG_REQ)
                                  || (actionFrmType == WLAN_HDD_GO_NEG_RESP))
                              extend_time = 2 * ACTION_FRAME_DEFAULT_WAIT;
                      else
                              extend_time = ACTION_FRAME_DEFAULT_WAIT;

                      if(completion_done(&pAdapter->rem_on_chan_ready_event))
                      {
                          if(VOS_TIMER_STATE_RUNNING ==
                            vos_timer_getCurrentState(&pRemainChanCtx->hdd_remain_on_chan_timer))
                          {
                              if (!VOS_IS_STATUS_SUCCESS(vos_timer_stop(
                                    &pRemainChanCtx->hdd_remain_on_chan_timer)))
                                  hddLog( LOGE, FL("Failed to stop hdd_remain_on_chan_timer"));
                              status = vos_timer_start(
                                  &pRemainChanCtx->hdd_remain_on_chan_timer,
                                            extend_time);
                              if (status != VOS_STATUS_SUCCESS)
                              {
                                  hddLog( LOGE, "%s: Remain on Channel timer start failed",
                                          __func__);
                              }
                          } else {
                              hddLog( LOG1, "%s: Rcvd action frame after timer expired",
                                      __func__);
                          }
                      } else {
                        // Buffer Packet
                          if(pRemainChanCtx->action_pkt_buff.frame_length == 0)
                          {
                             pRemainChanCtx->action_pkt_buff.frame_length = nFrameLength;
                             pRemainChanCtx->action_pkt_buff.freq = freq;
                             pRemainChanCtx->action_pkt_buff.frame_ptr
                                                = vos_mem_malloc(nFrameLength);
                             vos_mem_copy(pRemainChanCtx->action_pkt_buff.frame_ptr,
                                                pbFrames, nFrameLength);
                              hddLog( LOGE,"%s:"
                                 "Action Pkt Cached successfully !!!", __func__);
                          } else {
                              hddLog( LOGE,"%s:"
                                 "Frames are pending. dropping frame !!!", __func__);
                          }
                          mutex_unlock(&cfgState->remain_on_chan_ctx_lock);
                          return;
                      }
                 }
             }
             mutex_unlock(&cfgState->remain_on_chan_ctx_lock);

                if (((actionFrmType == WLAN_HDD_PROV_DIS_RESP) &&
                            (cfgState->actionFrmState == HDD_PD_REQ_ACK_PENDING)) ||
                        ((actionFrmType == WLAN_HDD_GO_NEG_RESP) &&
                         (cfgState->actionFrmState == HDD_GO_NEG_REQ_ACK_PENDING)))
                {
                    hddLog(LOG1, "%s: ACK_PENDING and But received RESP for Action frame ",
                            __func__);
                    cfgState->is_go_neg_ack_received = 1;
                    hdd_sendActionCnf(pAdapter, TRUE);
                }
            }
#ifdef FEATURE_WLAN_TDLS
            else if(pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET+1] == WLAN_HDD_PUBLIC_ACTION_TDLS_DISC_RESP)
            {
                u8 *mac = &pbFrames[WLAN_HDD_80211_FRM_DA_OFFSET+6];
                hddLog(VOS_TRACE_LEVEL_INFO,"[TDLS] TDLS Discovery Response," MAC_ADDRESS_STR " RSSI[%d] <--- OTA",
                 MAC_ADDR_ARRAY(mac),rxRssi);

                wlan_hdd_tdls_set_rssi(pAdapter, mac, rxRssi);
                wlan_hdd_tdls_recv_discovery_resp(pAdapter, mac);
                vos_tdls_tx_rx_mgmt_event(SIR_MAC_ACTION_TDLS,
                   SIR_MAC_ACTION_RX, SIR_MAC_MGMT_ACTION,
                   pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET+1],
                   &pbFrames[WLAN_HDD_80211_FRM_DA_OFFSET+6]);
            }
#endif
        }
        if(pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET] == WLAN_HDD_TDLS_ACTION_FRAME)
        {
            actionFrmType = pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET+1];
            if(actionFrmType >= MAX_TDLS_ACTION_FRAME_TYPE)
            {
                hddLog(VOS_TRACE_LEVEL_INFO,"[TDLS] Action type[%d] <--- OTA",
                                                            actionFrmType);
            }
            else
            {
                hddLog(VOS_TRACE_LEVEL_INFO,"[TDLS] %s <--- OTA",
                    tdls_action_frame_type[actionFrmType]);
            }
            vos_tdls_tx_rx_mgmt_event(SIR_MAC_ACTION_TDLS,
              SIR_MAC_ACTION_RX, SIR_MAC_MGMT_ACTION,
              actionFrmType, &pbFrames[WLAN_HDD_80211_FRM_DA_OFFSET+6]);

        }

        if((pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET] == WLAN_HDD_QOS_ACTION_FRAME)&&
             (pbFrames[WLAN_HDD_PUBLIC_ACTION_FRAME_OFFSET+1] == WLAN_HDD_QOS_MAP_CONFIGURE) )
        {
            sme_UpdateDSCPtoUPMapping(pHddCtx->hHal,
                pAdapter->hddWmmDscpToUpMap, pAdapter->sessionId);
        }
    }

    if (wlan_hdd_cfg80211_convert_rxmgmt_flags(rx_flags, &nl80211_flag))
        hddLog(LOG1, "Failed to convert RXMGMT flags :0x%x to nl80211 format",
                  rx_flags);
    //Indicate Frame Over Normal Interface
    hddLog( LOG1, FL("Indicate Frame over NL80211 Interface"));
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0)) || defined(WITH_BACKPORTS)
    cfg80211_rx_mgmt(pAdapter->dev->ieee80211_ptr, freq, rxRssi * 100, pbFrames,
                     nFrameLength, NL80211_RXMGMT_FLAG_ANSWERED | nl80211_flag);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,0))
    cfg80211_rx_mgmt(pAdapter->dev->ieee80211_ptr, freq, rxRssi * 100, pbFrames,
                     nFrameLength, NL80211_RXMGMT_FLAG_ANSWERED, GFP_ATOMIC);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0))
    cfg80211_rx_mgmt( pAdapter->dev->ieee80211_ptr, freq, rxRssi * 100,
                      pbFrames, nFrameLength,
                      GFP_ATOMIC );
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0))
    cfg80211_rx_mgmt( pAdapter->dev, freq, rxRssi * 100,
                      pbFrames, nFrameLength,
                      GFP_ATOMIC );
#else
    cfg80211_rx_mgmt( pAdapter->dev, freq,
                      pbFrames, nFrameLength,
                      GFP_ATOMIC );
#endif /* LINUX_VERSION_CODE */
}


