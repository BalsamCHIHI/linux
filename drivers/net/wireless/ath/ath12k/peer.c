// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "core.h"
#include "peer.h"
#include "debug.h"

static struct ath12k_ml_peer *ath12k_ml_peer_find(struct ath12k_hw *ah,
						  const u8 *addr)
{
	struct ath12k_ml_peer *ml_peer;

	lockdep_assert_held(&ah->data_lock);

	list_for_each_entry(ml_peer, &ah->ml_peers, list) {
		if (!ether_addr_equal(ml_peer->addr, addr))
			continue;

		return ml_peer;
	}

	return NULL;
}

struct ath12k_peer *ath12k_peer_find(struct ath12k_base *ab, int vdev_id,
				     const u8 *addr)
{
	struct ath12k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	list_for_each_entry(peer, &ab->peers, list) {
		if (peer->vdev_id != vdev_id)
			continue;
		if (!ether_addr_equal(peer->addr, addr))
			continue;

		return peer;
	}

	return NULL;
}

static struct ath12k_peer *ath12k_peer_find_by_pdev_idx(struct ath12k_base *ab,
							u8 pdev_idx, const u8 *addr)
{
	struct ath12k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	list_for_each_entry(peer, &ab->peers, list) {
		if (peer->pdev_idx != pdev_idx)
			continue;
		if (!ether_addr_equal(peer->addr, addr))
			continue;

		return peer;
	}

	return NULL;
}

struct ath12k_peer *ath12k_peer_find_by_addr(struct ath12k_base *ab,
					     const u8 *addr)
{
	struct ath12k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	list_for_each_entry(peer, &ab->peers, list) {
		if (!ether_addr_equal(peer->addr, addr))
			continue;

		return peer;
	}

	return NULL;
}

static struct ath12k_peer *ath12k_peer_find_by_ml_id(struct ath12k_base *ab,
						     int ml_peer_id)
{
	struct ath12k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	list_for_each_entry(peer, &ab->peers, list)
		if (ml_peer_id == peer->ml_peer_id)
			return peer;

	return NULL;
}

struct ath12k_peer *ath12k_peer_find_by_id(struct ath12k_base *ab,
					   int peer_id)
{
	struct ath12k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	if (peer_id & ATH12K_ML_PEER_ID_VALID)
		return ath12k_peer_find_by_ml_id(ab, peer_id);

	list_for_each_entry(peer, &ab->peers, list)
		if (peer_id == peer->peer_id)
			return peer;

	return NULL;
}

bool ath12k_peer_exist_by_vdev_id(struct ath12k_base *ab, int vdev_id)
{
	struct ath12k_peer *peer;

	spin_lock_bh(&ab->base_lock);

	list_for_each_entry(peer, &ab->peers, list) {
		if (vdev_id == peer->vdev_id) {
			spin_unlock_bh(&ab->base_lock);
			return true;
		}
	}
	spin_unlock_bh(&ab->base_lock);
	return false;
}

struct ath12k_peer *ath12k_peer_find_by_ast(struct ath12k_base *ab,
					    int ast_hash)
{
	struct ath12k_peer *peer;

	lockdep_assert_held(&ab->base_lock);

	list_for_each_entry(peer, &ab->peers, list)
		if (ast_hash == peer->ast_hash)
			return peer;

	return NULL;
}

void ath12k_peer_unmap_event(struct ath12k_base *ab, u16 peer_id)
{
	struct ath12k_peer *peer;

	spin_lock_bh(&ab->base_lock);

	peer = ath12k_peer_find_by_id(ab, peer_id);
	if (!peer) {
		ath12k_warn(ab, "peer-unmap-event: unknown peer id %d\n",
			    peer_id);
		goto exit;
	}

	ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "htt peer unmap vdev %d peer %pM id %d\n",
		   peer->vdev_id, peer->addr, peer_id);

	list_del(&peer->list);
	kfree(peer);
	wake_up(&ab->peer_mapping_wq);

exit:
	spin_unlock_bh(&ab->base_lock);
}

void ath12k_peer_map_event(struct ath12k_base *ab, u8 vdev_id, u16 peer_id,
			   u8 *mac_addr, u16 ast_hash, u16 hw_peer_id)
{
	struct ath12k_peer *peer;

	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find(ab, vdev_id, mac_addr);
	if (!peer) {
		peer = kzalloc(sizeof(*peer), GFP_ATOMIC);
		if (!peer)
			goto exit;

		peer->vdev_id = vdev_id;
		peer->peer_id = peer_id;
		peer->ast_hash = ast_hash;
		peer->hw_peer_id = hw_peer_id;
		ether_addr_copy(peer->addr, mac_addr);
		list_add(&peer->list, &ab->peers);
		wake_up(&ab->peer_mapping_wq);
	}

	ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "htt peer map vdev %d peer %pM id %d\n",
		   vdev_id, mac_addr, peer_id);

exit:
	spin_unlock_bh(&ab->base_lock);
}

static int ath12k_wait_for_peer_common(struct ath12k_base *ab, int vdev_id,
				       const u8 *addr, bool expect_mapped)
{
	int ret;

	ret = wait_event_timeout(ab->peer_mapping_wq, ({
				bool mapped;

				spin_lock_bh(&ab->base_lock);
				mapped = !!ath12k_peer_find(ab, vdev_id, addr);
				spin_unlock_bh(&ab->base_lock);

				(mapped == expect_mapped ||
				 test_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags));
				}), 3 * HZ);

	if (ret <= 0)
		return -ETIMEDOUT;

	return 0;
}

void ath12k_peer_cleanup(struct ath12k *ar, u32 vdev_id)
{
	struct ath12k_peer *peer, *tmp;
	struct ath12k_base *ab = ar->ab;

	lockdep_assert_held(&ar->conf_mutex);

	spin_lock_bh(&ab->base_lock);
	list_for_each_entry_safe(peer, tmp, &ab->peers, list) {
		if (peer->vdev_id != vdev_id)
			continue;

		ath12k_warn(ab, "removing stale peer %pM from vdev_id %d\n",
			    peer->addr, vdev_id);

		list_del(&peer->list);
		kfree(peer);
		ar->num_peers--;
	}

	spin_unlock_bh(&ab->base_lock);
}

static int ath12k_wait_for_peer_deleted(struct ath12k *ar, int vdev_id, const u8 *addr)
{
	return ath12k_wait_for_peer_common(ar->ab, vdev_id, addr, false);
}

int ath12k_wait_for_peer_delete_done(struct ath12k *ar, u32 vdev_id,
				     const u8 *addr)
{
	int ret;
	unsigned long time_left;

	ret = ath12k_wait_for_peer_deleted(ar, vdev_id, addr);
	if (ret) {
		ath12k_warn(ar->ab, "failed wait for peer deleted");
		return ret;
	}

	time_left = wait_for_completion_timeout(&ar->peer_delete_done,
						3 * HZ);
	if (time_left == 0) {
		ath12k_warn(ar->ab, "Timeout in receiving peer delete response\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int ath12k_peer_delete(struct ath12k *ar, u32 vdev_id, u8 *addr)
{
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	ret = ath12k_peer_delete_send(ar, vdev_id, addr);
	if (ret)
		return ret;

	ret = ath12k_wait_for_peer_delete_done(ar, vdev_id, addr);
	if (ret)
		return ret;

	ar->num_peers--;

	return 0;
}

static int ath12k_wait_for_peer_created(struct ath12k *ar, int vdev_id, const u8 *addr)
{
	return ath12k_wait_for_peer_common(ar->ab, vdev_id, addr, true);
}

int ath12k_peer_create(struct ath12k *ar, struct ath12k_link_vif *arvif,
		       struct ieee80211_sta *sta,
		       struct ath12k_wmi_peer_create_arg *arg)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ath12k_hw *ah = arvif->ahvif->ah;
	struct ath12k_peer *peer;
	int ret;
	struct ath12k_sta *ahsta;
	struct ath12k_link_sta *arsta;
	u8 link_id = arvif->link_id;
	u16 ml_peer_id;

	lockdep_assert_held(&ar->conf_mutex);

	if (ar->num_peers > (ar->max_num_peers - 1)) {
		ath12k_warn(ar->ab,
			    "failed to create peer due to insufficient peer entry resource in firmware\n");
		return -ENOBUFS;
	}

	spin_lock_bh(&ar->ab->base_lock);
	peer = ath12k_peer_find_by_pdev_idx(ar->ab, ar->pdev_idx, arg->peer_addr);
	if (peer) {
		spin_unlock_bh(&ar->ab->base_lock);
		return -EINVAL;
	}
	spin_unlock_bh(&ar->ab->base_lock);

	ret = ath12k_wmi_send_peer_create_cmd(ar, arg);
	if (ret) {
		ath12k_warn(ar->ab,
			    "failed to send peer create vdev_id %d ret %d\n",
			    arg->vdev_id, ret);
		return ret;
	}

	ret = ath12k_wait_for_peer_created(ar, arg->vdev_id,
					   arg->peer_addr);
	if (ret)
		return ret;

	spin_lock_bh(&ar->ab->base_lock);

	peer = ath12k_peer_find(ar->ab, arg->vdev_id, arg->peer_addr);
	if (!peer) {
		spin_unlock_bh(&ar->ab->base_lock);
		ath12k_warn(ar->ab, "failed to find peer %pM on vdev %i after creation\n",
			    arg->peer_addr, arg->vdev_id);

		reinit_completion(&ar->peer_delete_done);

		ret = ath12k_wmi_send_peer_delete_cmd(ar, arg->peer_addr,
						      arg->vdev_id);
		if (ret) {
			ath12k_warn(ar->ab, "failed to delete peer vdev_id %d addr %pM\n",
				    arg->vdev_id, arg->peer_addr);
			return ret;
		}

		ret = ath12k_wait_for_peer_delete_done(ar, arg->vdev_id,
						       arg->peer_addr);
		if (ret)
			return ret;

		return -ENOENT;
	}

	peer->pdev_idx = ar->pdev_idx;
	peer->sta = sta;

	if (vif->type == NL80211_IFTYPE_STATION) {
		arvif->ast_hash = peer->ast_hash;
		arvif->ast_idx = peer->hw_peer_id;
	}

	if (sta) {
		ahsta = ath12k_sta_to_ahsta(sta);
		arsta = rcu_dereference_protected(ahsta->link[link_id],
						  lockdep_is_held(&ah->conf_mutex));

		/* Fill ML info into created peer */
		if (sta->mlo) {
			ml_peer_id = ahsta->ml_peer_id;
			peer->ml_peer_id = ml_peer_id | ATH12K_ML_PEER_ID_VALID;
			ether_addr_copy(peer->ml_addr, sta->addr);
			/* the assoc link is considered primary for now */
			peer->primary_link = arsta->is_assoc_link;
		} else {
			peer->ml_peer_id = ATH12K_MLO_PEER_ID_INVALID;
			peer->primary_link = true;
		}
	}

	peer->sec_type = HAL_ENCRYPT_TYPE_OPEN;
	peer->sec_type_grp = HAL_ENCRYPT_TYPE_OPEN;

	ar->num_peers++;

	spin_unlock_bh(&ar->ab->base_lock);

	return 0;
}

static u16 ath12k_mac_alloc_ml_peer_id(struct ath12k_hw *ah)
{
	u16 ml_peer_id;

	lockdep_assert_held(&ah->conf_mutex);

	for (ml_peer_id = 0; ml_peer_id < ATH12K_MAX_MLO_PEERS; ml_peer_id++) {
		if (test_bit(ml_peer_id, ah->free_ml_peer_id_map))
			continue;

		set_bit(ml_peer_id, ah->free_ml_peer_id_map);
		break;
	}

	if (ml_peer_id == ATH12K_MAX_MLO_PEERS)
		ml_peer_id = ATH12K_MLO_PEER_ID_INVALID;

	return ml_peer_id;
}

int ath12k_ml_peer_create(struct ath12k_hw *ah, struct ieee80211_sta *sta)
{
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k_ml_peer *ml_peer;

	lockdep_assert_held(&ah->conf_mutex);

	if (!sta->mlo)
		return -EINVAL;

	spin_lock_bh(&ah->data_lock);
	ml_peer = ath12k_ml_peer_find(ah, sta->addr);
	if (ml_peer) {
		spin_unlock_bh(&ah->data_lock);
		ath12k_err(NULL, "ML peer(id=%d) exists already, unable to add new entry for %pM",
			   ml_peer->id, sta->addr);
		return -EEXIST;
	}

	ml_peer = kzalloc(sizeof(*ml_peer), GFP_ATOMIC);
	if (!ml_peer) {
		spin_unlock_bh(&ah->data_lock);
		ath12k_err(NULL, "unable to allocate new ML peer for %pM",
			   sta->addr);
		return -ENOMEM;
	}

	ahsta->ml_peer_id = ath12k_mac_alloc_ml_peer_id(ah);

	if (ahsta->ml_peer_id == ATH12K_MLO_PEER_ID_INVALID) {
		kfree(ml_peer);
		spin_unlock_bh(&ah->data_lock);
		ath12k_err(NULL, "unable to allocate ml peer id for sta %pM", sta->addr);
		return -ENOMEM;
	}

	ether_addr_copy(ml_peer->addr, sta->addr);
	ml_peer->id = ahsta->ml_peer_id;
	list_add(&ml_peer->list, &ah->ml_peers);
	spin_unlock_bh(&ah->data_lock);

	ath12k_dbg(NULL, ATH12K_DBG_MAC, "ML peer created for %pM id %d\n",
		   sta->addr, ahsta->ml_peer_id);
	return 0;
}

int ath12k_ml_peer_delete(struct ath12k_hw *ah, struct ieee80211_sta *sta)
{
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(sta);
	struct ath12k_ml_peer *ml_peer;

	lockdep_assert_held(&ah->conf_mutex);
	if (!sta->mlo)
		return -EINVAL;

	clear_bit(ahsta->ml_peer_id, ah->free_ml_peer_id_map);
	ahsta->ml_peer_id = ATH12K_MLO_PEER_ID_INVALID;

	spin_lock_bh(&ah->data_lock);
	ml_peer = ath12k_ml_peer_find(ah, sta->addr);
	if (!ml_peer) {
		spin_unlock_bh(&ah->data_lock);
		ath12k_err(NULL, "ML peer for %pM not found", sta->addr);
		return -EINVAL;
	}

	list_del(&ml_peer->list);
	kfree(ml_peer);
	spin_unlock_bh(&ah->data_lock);

	ath12k_dbg(NULL, ATH12K_DBG_MAC, "ML peer deleted for %pM\n",
		   sta->addr);
	return 0;
}

int ath12k_ml_link_peers_delete(struct ath12k_vif *ahvif, struct ath12k_sta *ahsta)
{
	struct ath12k_link_vif *arvif;
	struct ath12k_link_sta *arsta;
	struct ieee80211_sta *sta = ath12k_ahsta_to_sta(ahsta);
	struct ath12k *ar;
	int ret, err_ret = 0;
	u8 link_id = 0;
	struct ath12k_hw *ah = ahvif->ah;
	unsigned long links;

	lockdep_assert_held(&ah->conf_mutex);

	if (!sta->mlo)
		return -EINVAL;

	/* FW expects delete of all link peers at once before waiting for reception
	 * of peer unmap or delete responses
	 */
	links = ahsta->links_map;
	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = rcu_dereference_protected(ahvif->link[link_id],
						  lockdep_is_held(&ah->conf_mutex));
		arsta = rcu_dereference_protected(ahsta->link[link_id],
						  lockdep_is_held(&ah->conf_mutex));
		if (!arvif || !arsta)
			continue;

		ar = arvif->ar;
		if (!ar)
			continue;

		if (ahvif->vdev_type == WMI_VDEV_TYPE_STA) {
			ret = ath12k_mac_vdev_stop(arvif);
			if (ret)
				ath12k_warn(ar->ab, "failed to stop vdev %i: %d\n",
					    arvif->vdev_id, ret);
		}

		cancel_work_sync(&arsta->update_wk);

		mutex_lock(&ar->conf_mutex);
		ath12k_dp_peer_cleanup(ar, arvif->vdev_id, arsta->addr);

		ret = ath12k_peer_delete_send(ar, arvif->vdev_id, arsta->addr);
		if (ret) {
			mutex_unlock(&ar->conf_mutex);
			ath12k_warn(ar->ab,
				    "failed to delete peer vdev_id %d addr %pM ret %d\n",
				    arvif->vdev_id, arsta->addr, ret);
			err_ret = ret;
			continue;
		}
		mutex_unlock(&ar->conf_mutex);
	}

	/* Ensure all link peers are deleted and unmapped */
	links = ahsta->links_map;
	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		arvif = rcu_dereference_protected(ahvif->link[link_id],
						  lockdep_is_held(&ah->conf_mutex));
		arsta = rcu_dereference_protected(ahsta->link[link_id],
						  lockdep_is_held(&ah->conf_mutex));
		if (!arvif || !arsta)
			continue;

		mutex_lock(&ar->conf_mutex);
		ret = ath12k_wait_for_peer_delete_done(ar, arvif->vdev_id, arsta->addr);
		if (ret) {
			err_ret = ret;
			mutex_unlock(&ar->conf_mutex);
			continue;
		}
		ar->num_peers--;
		mutex_unlock(&ar->conf_mutex);
	}

	return err_ret;
}

int ath12k_peer_delete_send(struct ath12k *ar, u32 vdev_id, const u8 *addr)
{
	struct ath12k_base *ab = ar->ab;
	int ret;

	lockdep_assert_held(&ar->conf_mutex);

	reinit_completion(&ar->peer_delete_done);

	ret = ath12k_wmi_send_peer_delete_cmd(ar, addr, vdev_id);
	if (ret) {
		ath12k_warn(ab,
			    "failed to delete peer vdev_id %d addr %pM ret %d\n",
			    vdev_id, addr, ret);
		return ret;
	}

	return 0;
}

void ath12k_peer_mlo_map_event(struct ath12k_base *ab, struct sk_buff *skb)
{
	struct ath12k_htt_mlo_peer_map_msg *msg;
	u16 ml_peer_id;
	struct ath12k_peer *peer;
	u16 mld_mac_h16;
	u8 mld_addr[ETH_ALEN];

	msg = (struct ath12k_htt_mlo_peer_map_msg *)skb->data;

	ml_peer_id = u32_get_bits(msg->info0,
				  ATH12K_HTT_MLO_PEER_MAP_INFO0_PEER_ID);

	ml_peer_id |= ATH12K_ML_PEER_ID_VALID;

	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find_by_id(ab, ml_peer_id);

	/* TODO a sync wait to check ml peer map success or delete
	 * ml peer info in all link peers and make peer assoc failure
	 * TBA after testing basic changes
	 */
	if (!peer) {
		ath12k_warn(ab, "ml peer %d not found", ml_peer_id);
		spin_unlock_bh(&ab->base_lock);
		return;
	}

	mld_mac_h16 = u32_get_bits(msg->mac_addr.mac_addr_h16,
				   ATH12K_HTT_MLO_PEER_MAP_MAC_ADDR_H16);
	ath12k_dp_get_mac_addr(msg->mac_addr.mac_addr_l32, mld_mac_h16, mld_addr);

	WARN_ON(memcmp(mld_addr, peer->ml_addr, ETH_ALEN));

	spin_unlock_bh(&ab->base_lock);

	ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "htt MLO peer map peer %pM id %d\n",
		   mld_addr, ml_peer_id);

	/* TODO rx queue setup for the ML peer */
}

void ath12k_peer_mlo_unmap_event(struct ath12k_base *ab, struct sk_buff *skb)
{
	struct ath12k_htt_mlo_peer_unmap_msg *msg;
	u16 ml_peer_id;

	msg = (struct ath12k_htt_mlo_peer_unmap_msg *)skb->data;

	ml_peer_id = u32_get_bits(msg->info0, ATH12K_HTT_MLO_PEER_UNMAP_PEER_ID);

	ml_peer_id |= ATH12K_ML_PEER_ID_VALID;

	ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "htt MLO peer unmap peer ml id %d\n",
		   ml_peer_id);
}
