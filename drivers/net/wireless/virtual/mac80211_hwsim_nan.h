// SPDX-License-Identifier: GPL-2.0-only
/*
 * mac80211_hwsim_nan - NAN software simulation for mac80211_hwsim
 * Copyright (C) 2025-2026 Intel Corporation
 */

#ifndef __MAC80211_HWSIM_NAN_H
#define __MAC80211_HWSIM_NAN_H

enum mac80211_hwsim_nan_phase {
	MAC80211_HWSIM_NAN_PHASE_SCAN,
	MAC80211_HWSIM_NAN_PHASE_WARMUP,
	MAC80211_HWSIM_NAN_PHASE_UP,
};

enum mac80211_hwsim_nan_role {
	MAC80211_HWSIM_NAN_ROLE_MASTER,
	MAC80211_HWSIM_NAN_ROLE_SYNC,
	MAC80211_HWSIM_NAN_ROLE_NON_SYNC,
};

#define NAN_ATTR_MASTER_INDICATION		0x00
#define NAN_ATTR_CLUSTER_INFO			0x01

#define for_each_nan_attr(_attr, _data, _datalen)			\
	for (_attr = (const struct ieee80211_nan_attr *)(_data);	\
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_attr >=	\
		(int)sizeof(*_attr) &&					\
	     (const u8 *)(_data) + (_datalen) - (const u8 *)_attr >=	\
		(int)sizeof(*_attr) + le16_to_cpu(_attr->length);	\
	     _attr = (const struct ieee80211_nan_attr *)		\
		(_attr->data + le16_to_cpu(_attr->length)))

struct ieee80211_hw;
struct ieee80211_rx_status;

u64 ieee80211_calculate_rx_timestamp(struct ieee80211_hw *hw,
				     struct ieee80211_rx_status *status,
				     unsigned int mpdu_len,
				     unsigned int mpdu_offset);

struct ieee80211_nan_attr {
	u8 attr;
	__le16 length;
	u8 data[];
} __packed;

struct ieee80211_nan_master_indication {
	u8 master_pref;
	u8 random_factor;
} __packed;

struct ieee80211_nan_anchor_master_info {
	union {
		__le64 master_rank;
		struct {
			u8 master_addr[ETH_ALEN];
			u8 random_factor;
			u8 master_pref;
		} __packed;
	} __packed;
	u8 hop_count;
	__le32 ambtt;
} __packed;

struct mac80211_hwsim_nan_data {
	struct ieee80211_vif *device_vif;
	u8 bands;

	struct hrtimer slot_timer;
	struct hrtimer resume_txqs_timer;
	bool notify_dw;

	struct hrtimer discovery_beacon_timer;

	/* Later members are protected by this lock */
	spinlock_t state_lock;

	u8 master_pref;
	u8 random_factor;

	u8 random_factor_valid_dwst;

	enum mac80211_hwsim_nan_phase phase;
	enum mac80211_hwsim_nan_role role;

	u8 cluster_id[ETH_ALEN];

	struct ieee80211_nan_anchor_master_info current_ami;
	struct ieee80211_nan_anchor_master_info last_ami;

	/* Wi-Fi Aware version 4.0, section 3.3.6.1 and 3.3.6.2 */
	int master_transition_score;
	/* Wi-Fi Aware version 4.0, section 3.3.6.3 and 3.3.6.4 */
	int sync_transition_score;

	bool tsf_adjusted;
	bool tsf_discontinuity;

	/*
	 * Local schedule - stores channel definition for each 16TU slot.
	 * Derived from NMI vif->cfg.nan_schedule. chan == NULL means not
	 * available in that slot (except DW which is implicit).
	 */
	struct cfg80211_chan_def local_sched[CFG80211_NAN_SCHED_NUM_TIME_SLOTS];
};

enum hrtimer_restart
mac80211_hwsim_nan_slot_timer(struct hrtimer *timer);
enum hrtimer_restart
mac80211_hwsim_nan_resume_txqs_timer(struct hrtimer *timer);
enum hrtimer_restart
mac80211_hwsim_nan_discovery_beacon_timer(struct hrtimer *timer);

int mac80211_hwsim_nan_start(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_nan_conf *conf);

int mac80211_hwsim_nan_stop(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif);

int mac80211_hwsim_nan_change_config(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct cfg80211_nan_conf *conf,
				     u32 changes);

int mac80211_hwsim_nan_peer_sched_changed(struct ieee80211_hw *hw,
					  struct ieee80211_sta *sta);

bool mac80211_hwsim_nan_txq_transmitting(struct ieee80211_hw *hw,
					 struct ieee80211_txq *txq);

void mac80211_hwsim_nan_get_tx_chandef(struct ieee80211_hw *hw,
				       struct cfg80211_chan_def *chandef);

bool mac80211_hwsim_nan_receive(struct ieee80211_hw *hw,
				struct ieee80211_channel *channel,
				struct ieee80211_rx_status *rx_status);

void mac80211_hwsim_nan_rx(struct ieee80211_hw *hw,
			   struct sk_buff *skb);

void mac80211_hwsim_nan_local_sched_changed(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif);

#endif /* __MAC80211_HWSIM_NAN_H */
