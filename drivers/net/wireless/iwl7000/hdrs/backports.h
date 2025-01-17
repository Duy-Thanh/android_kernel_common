/*
 * ChromeOS backport definitions
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2024 Intel Corporation
 */

/* backport wiphy_ext_feature_set/_isset
 *
 * To do so, define our own versions thereof that check for a negative
 * feature index and in that case ignore it entirely. That allows us to
 * define the ones that the cfg80211 version doesn't support to -1.
 */
static inline void iwl7000_wiphy_ext_feature_set(struct wiphy *wiphy, int ftidx)
{
	if (ftidx < 0)
		return;
	wiphy_ext_feature_set(wiphy, ftidx);
}

static inline bool iwl7000_wiphy_ext_feature_isset(struct wiphy *wiphy,
						   int ftidx)
{
	if (ftidx < 0)
		return false;
	return wiphy_ext_feature_isset(wiphy, ftidx);
}
#define wiphy_ext_feature_set iwl7000_wiphy_ext_feature_set
#define wiphy_ext_feature_isset iwl7000_wiphy_ext_feature_isset

void ieee80211_fragment_element(struct sk_buff *skb, u8 *len_pos, u8 frag_id);

static inline void
_ieee80211_set_sband_iftype_data(struct ieee80211_supported_band *sband,
				 const struct ieee80211_sband_iftype_data *iftd,
				 u16 n_iftd)
{
	sband->iftype_data = iftd;
	sband->n_iftype_data = n_iftd;
}

void wiphy_delayed_work_timer(struct timer_list *t);

#define wiphy_delayed_work_init LINUX_BACKPORT(wiphy_delayed_work_init)
static inline void wiphy_delayed_work_init(struct wiphy_delayed_work *dwork,
					   wiphy_work_func_t func)
{
	timer_setup(&dwork->timer, wiphy_delayed_work_timer, 0);
	wiphy_work_init(&dwork->work, func);
}

void wiphy_work_queue(struct wiphy *wiphy, struct wiphy_work *work);
void wiphy_work_cancel(struct wiphy *wiphy, struct wiphy_work *work);

void wiphy_delayed_work_queue(struct wiphy *wiphy,
			      struct wiphy_delayed_work *dwork,
			      unsigned long delay);
void wiphy_delayed_work_cancel(struct wiphy *wiphy,
			       struct wiphy_delayed_work *dwork);

void wiphy_work_flush(struct wiphy *wiphy, struct wiphy_work *work);
void wiphy_delayed_work_flush(struct wiphy *wiphy,
			      struct wiphy_delayed_work *work);

#ifndef for_each_sband_iftype_data
#define for_each_sband_iftype_data(sband, i, iftd)	\
	for (i = 0, iftd = &(sband)->iftype_data[i];	\
	     i < (sband)->n_iftype_data;		\
	     i++, iftd = &(sband)->iftype_data[i])
#endif

/* older cfg80211 requires wdev to be locked */
#define WRAP_LOCKED(sym) wdev_locked_ ## sym

static inline void
WRAP_LOCKED(cfg80211_links_removed)(struct net_device *dev, u16 removed_links)
{
	mutex_lock(&dev->ieee80211_ptr->mtx);
	cfg80211_links_removed(dev, removed_links);
	mutex_unlock(&dev->ieee80211_ptr->mtx);
}
#define cfg80211_links_removed WRAP_LOCKED(cfg80211_links_removed)
static inline u32
iwl7000_ieee80211_mandatory_rates(struct ieee80211_supported_band *sband)
{
	return ieee80211_mandatory_rates(sband);
}
#define ieee80211_mandatory_rates iwl7000_ieee80211_mandatory_rates

static inline bool LINUX_BACKPORT(napi_schedule)(struct napi_struct *n)
{
	if (napi_schedule_prep(n)) {
		__napi_schedule(n);
		return true;
	}

	return false;
}
#define napi_schedule LINUX_BACKPORT(napi_schedule)

#ifdef CONFIG_CFG80211_DEBUGFS
static inline
ssize_t wiphy_locked_debugfs_read(struct wiphy *wiphy, struct file *file,
				  char *buf, size_t bufsize,
				  char __user *userbuf, size_t count,
				  loff_t *ppos,
				  ssize_t (*handler)(struct wiphy *wiphy,
						     struct file *file,
						     char *buf,
						     size_t bufsize,
						     void *data),
				  void *data)
{
	ssize_t ret = -EINVAL;

#if LINUX_VERSION_IS_GEQ(5,12,0)
	wiphy_lock(wiphy);
#else
	rtnl_lock();
#endif
	ret = handler(wiphy, file, buf, bufsize, data);
#if LINUX_VERSION_IS_GEQ(5,12,0)
	wiphy_unlock(wiphy);
#else
	rtnl_unlock();
#endif

	if (ret >= 0)
		ret = simple_read_from_buffer(userbuf, count, ppos, buf, ret);

	return ret;
}

static inline
ssize_t wiphy_locked_debugfs_write(struct wiphy *wiphy, struct file *file,
				   char *buf, size_t bufsize,
				   const char __user *userbuf, size_t count,
				   ssize_t (*handler)(struct wiphy *wiphy,
						      struct file *file,
						      char *buf,
						      size_t count,
						      void *data),
				   void *data)
{
	ssize_t ret;

	if (count >= sizeof(buf))
		return -E2BIG;

	if (copy_from_user(buf, userbuf, count))
		return -EFAULT;
	buf[count] = '\0';

#if LINUX_VERSION_IS_GEQ(5,12,0)
	wiphy_lock(wiphy);
#else
	rtnl_lock();
#endif
	ret = handler(wiphy, file, buf, bufsize, data);
#if LINUX_VERSION_IS_GEQ(5,12,0)
	wiphy_unlock(wiphy);
#else
	rtnl_unlock();
#endif

	return ret;
}
#endif

static inline void cfg80211_schedule_channels_check(struct wireless_dev *wdev)
{
}
#define NL80211_EXT_FEATURE_DFS_CONCURRENT -1
#define NL80211_RRF_DFS_CONCURRENT 0

bool
ieee80211_uhb_power_type_valid(struct ieee80211_mgmt *mgmt, size_t len,
			       struct ieee80211_channel *channel);

#define IEEE80211_CHAN_NO_6GHZ_VLP_CLIENT BIT(21)
#define IEEE80211_CHAN_NO_6GHZ_AFC_CLIENT BIT(22)

#define NL80211_RRF_NO_6GHZ_VLP_CLIENT BIT(22)
#define NL80211_RRF_NO_6GHZ_AFC_CLIENT BIT(23)

ssize_t cfg80211_defragment_element(const struct element *elem, const u8 *ies,
				    size_t ieslen, u8 *data, size_t data_len,
				    u8 frag_id);

enum cfg80211_rnr_iter_ret {
	RNR_ITER_CONTINUE,
	RNR_ITER_BREAK,
	RNR_ITER_ERROR,
};

bool cfg80211_iter_rnr(const u8 *elems, size_t elems_len,
		       enum cfg80211_rnr_iter_ret
		       (*iter)(void *data, u8 type,
			       const struct ieee80211_neighbor_ap_info *info,
			       const u8 *tbtt_info, u8 tbtt_info_len),
		       void *iter_data);

#if LINUX_VERSION_IS_LESS(6,0,0)
#define cfg80211_ch_switch_notify(dev, chandef, link_id) cfg80211_ch_switch_notify(dev, chandef)
#else
#define cfg80211_ch_switch_notify(dev, chandef, link_id) cfg80211_ch_switch_notify(dev, chandef, link_id, 0)
#endif

#define NL80211_EXT_FEATURE_SPP_AMSDU_SUPPORT -1
#define ASSOC_REQ_SPP_AMSDU BIT(7)
#define NL80211_STA_FLAG_SPP_AMSDU 8
bool ieee80211_operating_class_to_chandef(u8 operating_class,
					  struct ieee80211_channel *chan,
					  struct cfg80211_chan_def *chandef);

#define IEEE80211_CHAN_CAN_MONITOR 0

int nl80211_chan_width_to_mhz(enum nl80211_chan_width chan_width);
int cfg80211_chandef_primary(const struct cfg80211_chan_def *chandef,
			     enum nl80211_chan_width primary_width,
			     u16 *punctured);

#if LINUX_VERSION_IS_LESS(5,11,0)
static inline void
LINUX_BACKPORT(cfg80211_ch_switch_started_notify)(struct net_device *dev,
						  struct cfg80211_chan_def *chandef,
						  unsigned int link_id, u8 count,
						  bool quiet)
{
	cfg80211_ch_switch_started_notify(dev, chandef, count);
}
#define cfg80211_ch_switch_started_notify LINUX_BACKPORT(cfg80211_ch_switch_started_notify)

#elif LINUX_VERSION_IS_LESS(6,1,0)
static inline void
LINUX_BACKPORT(cfg80211_ch_switch_started_notify)(struct net_device *dev,
						  struct cfg80211_chan_def *chandef,
						  unsigned int link_id, u8 count,
						  bool quiet)
{
	cfg80211_ch_switch_started_notify(dev, chandef, count, quiet);
}
#define cfg80211_ch_switch_started_notify LINUX_BACKPORT(cfg80211_ch_switch_started_notify)
#else
static inline void
LINUX_BACKPORT(cfg80211_ch_switch_started_notify)(struct net_device *dev,
						  struct cfg80211_chan_def *chandef,
						  unsigned int link_id, u8 count,
						  bool quiet)
{
	cfg80211_ch_switch_started_notify(dev, chandef, link_id, count, quiet, 0);
}
#define cfg80211_ch_switch_started_notify LINUX_BACKPORT(cfg80211_ch_switch_started_notify)
#endif

#ifdef CONFIG_THERMAL
#define THERMAL_TRIP_FLAG_RW_TEMP       BIT(0)
static inline struct thermal_zone_device *
backport_thermal_zone_device_register_with_trips(const char *type,
						 struct thermal_trip *trips,
						 int num_trips, void *devdata,
						 struct thermal_zone_device_ops *ops,
						 struct thermal_zone_params *tzp,
						 int passive_delay,
						 int polling_delay)
{
#if LINUX_VERSION_IS_LESS(6,0,0)
	return thermal_zone_device_register(type, num_trips, 0, devdata, ops, tzp,
					    passive_delay, polling_delay);
#else
#undef thermal_trip
	return thermal_zone_device_register_with_trips(type,
						       (struct thermal_trip *)(void *) trips,
						       num_trips,
						       0, devdata,
						       ops, tzp, passive_delay,
						       polling_delay);
#define thermal_trip backport_thermal_trip
#endif /* < 6,6,0 */
#define thermal_zone_device_register_with_trips LINUX_BACKPORT(thermal_zone_device_register_with_trips)
}

/* This function was added in 6,6,0 already, but struct thermal_trip isn't */
#if LINUX_VERSION_IS_GEQ(6,0,0)
#define for_each_thermal_trip LINUX_BACKPORT(for_each_thermal_trip)
static inline
int for_each_thermal_trip(struct thermal_zone_device *tz,
			  int (*cb)(struct thermal_trip *, void *),
			  void *data)
{
	struct thermal_trip *trip;
	struct thermal_trip *trips = (void *)tz->trips;
	int ret;

	for (trip = trips; trip - trips < tz->num_trips; trip++) {
		ret = cb(trip, data);
		if (ret)
			return ret;
	}

	return 0;
}
#endif /* >= 6,0,0 */
#endif /* CONFIG_THERMAL */

static inline struct net_device *alloc_netdev_dummy(int sizeof_priv)
{
	struct net_device *dev;
	dev = kzalloc(sizeof(*dev) +
		      ALIGN(sizeof(struct net_device), NETDEV_ALIGN) +
		      sizeof_priv,
		      GFP_KERNEL);
	if (!dev)
		return NULL;
	init_dummy_netdev(dev);
	return dev;
}

static inline void LINUX_BACKPORT(free_netdev)(struct net_device *dev)
{
	if (dev->reg_state == NETREG_DUMMY) {
		kfree(dev);
		return;
	}
	free_netdev(dev);
}
#define free_netdev LINUX_BACKPORT(free_netdev)


enum ieee80211_ap_reg_power {
	IEEE80211_REG_UNSET_AP,
	IEEE80211_REG_LPI_AP,
	IEEE80211_REG_SP_AP,
	IEEE80211_REG_VLP_AP,
};

/* upstream numbers */
#define NL80211_RRF_ALLOW_6GHZ_VLP_AP		BIT(24)
#define IEEE80211_CHAN_ALLOW_6GHZ_VLP_AP	BIT(25)

struct cfg80211_iface_usage {
	u32 types_mask;
};
