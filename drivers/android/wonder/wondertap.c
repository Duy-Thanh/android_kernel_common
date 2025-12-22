#define pr_fmt(fmt) "[wonder][wondertap] " fmt
#define LOG_MODULE_NAME "wondertap"

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/etherdevice.h>
#include <linux/android/wondertap.h>

#include "wondertap_internal.h"

static bool wondertap_is_up(struct wondertap_data *wondertap)
{
	return wondertap->state == WONDERTAP_STATE_UP;
}

int wondertap_init(struct wondertap_data *wondertap, const struct wondertap_init_params *unused)
{
	struct wondertap_init_params params;
	int ret = -EOPNOTSUPP;

	mutex_lock(&wondertap->lock);
	if (wondertap_is_up(wondertap)) {
		pr_warn("Already initialized\n");
		ret = -EBUSY;
		goto out;
	}

	if (!((wondertap->cache_flags & WONDERTAP_CACHE_FREQ_SET) &&
		(wondertap->cache_flags & WONDERTAP_CACHE_TX_RATE_SET) &&
		(wondertap->cache_flags & WONDERTAP_CACHE_BSSID_SET) &&
		(wondertap->cache_flags & WONDERTAP_CACHE_COUNTRY_CODE_SET))) {
		pr_err("Init failed: Missing cached settings. flags=0x%x\n",
						wondertap->cache_flags);
		ret = -EINVAL;
		goto out;
	}

	if (wondertap->wonder_ops && wondertap->wonder_ops->init) {
		eth_random_addr(wondertap->mac_addr);
		params.channel = wondertap->cached_freq;
		params.tx_rate = wondertap->cached_tx_rate;
		memcpy(params.bssid, wondertap->cached_bssid, ETH_ALEN);
		memcpy(params.mac_addr, wondertap->mac_addr, ETH_ALEN);
		memcpy(params.country_code, wondertap->cached_country_code, sizeof(wondertap->cached_country_code));

		ret = wondertap->wonder_ops->init(&wondertap->vendor_handle, &params);
		if (ret == 0) {
			wondertap->state = WONDERTAP_STATE_UP;
			pr_debug("Vendor init successful. State set to UP.\n");
		} else {
			pr_err("Vendor init failed: %d\n", ret);
			goto out;
		}
	} else {
		pr_err("Vendor operation 'init' is not implemented\n");
		ret = -EOPNOTSUPP;
	}

out:
	mutex_unlock(&wondertap->lock);
	return ret;
}

void wondertap_deinit(struct wondertap_data *wondertap)
{
	struct wondertap_deinit_params params;

	mutex_lock(&wondertap->lock);
	if (wondertap->wonder_ops && wondertap->wonder_ops->deinit) {
		memset(&params, 0, sizeof(params));
		memcpy(params.country_code, wondertap->cached_country_code, sizeof(wondertap->cached_country_code));
		pr_debug("========== [ Wondertap Vendor Deinit ] ==========\n");
		pr_debug("    Country: %.2s\n", params.country_code);
		pr_debug("=================================================\n");
		wondertap->wonder_ops->deinit(wondertap->vendor_handle, &params);
	} else {
		pr_err("Vendor operation 'deinit' is not implemented\n");
	}
	wondertap->state = WONDERTAP_STATE_DOWN;
	mutex_unlock(&wondertap->lock);
}

int wondertap_set_freq(struct wondertap_data *wondertap, const struct wondertap_set_freq_params *params)
{
	int ret = -EOPNOTSUPP;

	mutex_lock(&wondertap->lock);
	wondertap->cached_freq = *params;
	wondertap->cache_flags |= WONDERTAP_CACHE_FREQ_SET;
	if (wondertap_is_up(wondertap)) {
		if (wondertap->wonder_ops && wondertap->wonder_ops->set_freq) {
			ret = wondertap->wonder_ops->set_freq(wondertap->vendor_handle, params);
		} else {
			pr_err("Vendor operation 'set_freq' is not implemented\n");
			ret = -EOPNOTSUPP;
		}
	} else {
		pr_warn("wondertap is not active; caching incoming channel settings.\n");
		ret = 0;
	}

	mutex_unlock(&wondertap->lock);
	return ret;
}

int wondertap_set_filter(struct wondertap_data *wondertap, enum wondertap_filter_type filter_type,
			 const void *params)
{
	int ret = -EOPNOTSUPP;

	mutex_lock(&wondertap->lock);
	if (wondertap_is_up(wondertap)) {
		if (wondertap->wonder_ops && wondertap->wonder_ops->set_filter) {
			ret = wondertap->wonder_ops->set_filter(wondertap->vendor_handle,
				filter_type, params);
		} else {
			pr_err("Vendor operation 'set_filter' is not implemented\n");
			ret = -EOPNOTSUPP;
		}
	} else {
		pr_err("wondertap is invalid or not up.\n");
		ret = -ENODEV;
	}

	mutex_unlock(&wondertap->lock);
	return ret;
}

int wondertap_set_fixed_tx_rate(struct wondertap_data *wondertap, const struct wondertap_fixed_tx_rate_params *params)
{
	int ret = -EOPNOTSUPP;

	mutex_lock(&wondertap->lock);
	wondertap->cached_tx_rate = *params;
	wondertap->cache_flags |= WONDERTAP_CACHE_TX_RATE_SET;
	if (wondertap_is_up(wondertap)) {
		if (wondertap->wonder_ops && wondertap->wonder_ops->set_fixed_tx_rate) {
			ret = wondertap->wonder_ops->set_fixed_tx_rate(wondertap->vendor_handle, params);
		} else {
			pr_err("Vendor operation 'set_fixed_tx_rate' is not implemented\n");
			ret = -EOPNOTSUPP;
		}
	} else {
		pr_warn("wondertap is not active; caching incoming fixed TX rate settings.\n");
		ret = 0;
	}

	mutex_unlock(&wondertap->lock);
	return ret;
}

int wondertap_set_tx_rate_mask(struct wondertap_data *wondertap, const struct wondertap_tx_rate_mask_params *params)
{
	int ret = -EOPNOTSUPP;

	mutex_lock(&wondertap->lock);
	if (wondertap_is_up(wondertap)) {
		if (wondertap->wonder_ops && wondertap->wonder_ops->set_tx_rate_mask) {
			return wondertap->wonder_ops->set_tx_rate_mask(wondertap->vendor_handle, params);
		} else {
			pr_err("Vendor operation 'set_tx_rate_mask' is not implemented\n");
			ret = -EOPNOTSUPP;
		}
	} else {
		pr_err("wondertap is invalid or not up.\n");
		ret = -ENODEV;
	}

	mutex_unlock(&wondertap->lock);
	return ret;
}

int wondertap_set_reg(struct wondertap_data *wondertap, const char *country_code)
{
	mutex_lock(&wondertap->lock);

	memcpy(wondertap->cached_country_code, country_code, sizeof(wondertap->cached_country_code));
	wondertap->cache_flags |= WONDERTAP_CACHE_COUNTRY_CODE_SET;

	pr_err("Update country code=%s\n", wondertap->cached_country_code);

	mutex_unlock(&wondertap->lock);

	return 0;
}

int wondertap_get_capabilities(struct wondertap_data *wondertap, struct wondertap_capability *features)
{
	if (wondertap->wonder_ops && wondertap->wonder_ops->get_capabilities) {
		return wondertap->wonder_ops->get_capabilities(wondertap->vendor_handle, features);
	}

	pr_err("Vendor operation 'get_capabilities' is not implemented\n");
	return -EOPNOTSUPP;
}

int wondertap_get_interface_mac_address(struct wondertap_data *wondertap, u8 (*mac_addr)[ETH_ALEN])
{
	int ret = -EOPNOTSUPP;

	mutex_lock(&wondertap->lock);
	if (wondertap_is_up(wondertap)) {
		memcpy(*mac_addr, wondertap->mac_addr, sizeof(u8) * ETH_ALEN);
		ret = 0;
	} else {
		pr_err("wondertap is invalid or not up.\n");
		ret = -ENODEV;
	}

	mutex_unlock(&wondertap->lock);
	return ret;
}

int wondertap_set_bssid_filter(struct wondertap_data *wondertap, const u8 *bssid)
{
	mutex_lock(&wondertap->lock);
	memcpy(wondertap->cached_bssid, bssid, sizeof(u8) * ETH_ALEN);
	wondertap->cache_flags |= WONDERTAP_CACHE_BSSID_SET;
	pr_warn("Caching incoming BSSID filter settings.\n");
	mutex_unlock(&wondertap->lock);
	return 0;
}
