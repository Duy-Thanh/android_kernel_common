// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#include "ufshcd.h"
#include "ufshcd-crypto.h"

/*TODO: worry about endianness and cpu_to_le32 */

bool ufshcd_hba_is_crypto_supported(struct ufs_hba *hba)
{
	return hba->crypto_capabilities.reg_val != 0;
}

bool ufshcd_is_crypto_enabled(struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_CRYPTO;
}

bool crypto_cfg_slot_in_bounds(struct ufs_hba *hba, unsigned int slot)
{
	/**
	 * The actual number of configurations supported is (CFGC+1), so slot
	 * numbers range from 0 to config_count inclusive.
	 */
	return slot <= hba->crypto_capabilities.config_count;
}

static int get_ufs_data_mask_for_data_unit_size(unsigned int data_unit_size,
						u8 *ufs_data_mask)
{
	if (data_unit_size < 512 || data_unit_size > 65536 ||
	    !is_power_of_2(data_unit_size)) {
		return -EINVAL;
	}

	*ufs_data_mask = data_unit_size / 512;

	return 0;
}

static size_t get_keysize_bytes(enum ufs_crypto_key_size crypto_key_size)
{
	switch (crypto_key_size) {
	case UFS_CRYPTO_KEY_SIZE_128: return 16;
	case UFS_CRYPTO_KEY_SIZE_192: return 24;
	case UFS_CRYPTO_KEY_SIZE_256: return 32;
	case UFS_CRYPTO_KEY_SIZE_512: return 64;
	default: return 0;
	}

	return 0;
}

/**
 * ufshcd_crypto_cfg_entry_write_key - Write a key into a crypto_cfg_entry
 *
 *	Writes the key with the appropriate format - for AES_XTS,
 *	the first half of the key is copied as is, the second half is
 *	copied with an offset halfway into the cfg->crypto_key array.
 *	For the other supported crypto algs, the key is just copied.
 *
 * @cfg: The crypto config to write to
 * @key: The key to write
 * @cap: The crypto capability (which specifies the crypto alg and key size)
 *
 * Returns 0 on success, or -errno
 */
static int ufshcd_crypto_cfg_entry_write_key(union ufs_crypto_cfg_entry *cfg,
					     const u8 *key,
					     union ufs_crypto_cap_entry cap)
{
	size_t key_size_bytes = get_keysize_bytes(cap.key_size);

	if (key_size_bytes == 0)
		return -EINVAL;

	switch (cap.algorithm_id) {
	case UFS_CRYPTO_ALG_AES_XTS: {
		key_size_bytes *= 2;
		if (key_size_bytes > UFS_CRYPTO_KEY_MAX_SIZE)
			return -EINVAL;

		memcpy(cfg->crypto_key, key, key_size_bytes/2);
		memcpy(cfg->crypto_key + UFS_CRYPTO_KEY_MAX_SIZE/2,
		       key + key_size_bytes/2, key_size_bytes/2);
		return 0;
	}
	case UFS_CRYPTO_ALG_BITLOCKER_AES_CBC: // fallthrough
	case UFS_CRYPTO_ALG_AES_ECB: // fallthrough
	case UFS_CRYPTO_ALG_ESSIV_AES_CBC: {
		memcpy(cfg->crypto_key, key, key_size_bytes);
		return 0;
	}
	}

	return -EINVAL;
}

static void program_key(struct ufs_hba *hba,
			const union ufs_crypto_cfg_entry *cfg,
			int slot)
{
	int c;

	/**
	 * Write the crypto cfg to device.
	 * Ensure that CFGE is written first, so that
	 * in case a reset happens while key is programmed,
	 * it is definitely cleared.
	 */
	ufshcd_writel(hba, cfg->reg_val[16],
		      hba->crypto_cfg_register +
		      slot * sizeof(*cfg) + 16 * sizeof(cfg->reg_val[0]));
	mb();
	for (c = 0; c < sizeof(*cfg)/sizeof(cfg->reg_val[0]); c++) {
		if (c == 16)
			continue;
		ufshcd_writel(hba, cfg->reg_val[c],
			      hba->crypto_cfg_register +
			      slot * sizeof(*cfg) +
			      c * sizeof(cfg->reg_val[0]));
	}
	mb();
}

static int ufshcd_crypto_keyslot_program(void *hba_p, const u8 *key,
			      unsigned int data_unit_size,
			      unsigned int cap_idx,
			      unsigned int slot)
{
	struct ufs_hba *hba = hba_p;
	int c = 0;
	u8 data_unit_mask;
	union ufs_crypto_cfg_entry cfg;
	union ufs_crypto_cfg_entry *cfg_arr = hba->crypto_priv;

	if (!ufshcd_is_crypto_enabled(hba) ||
	    !crypto_cfg_slot_in_bounds(hba, slot) ||
	    cap_idx >= hba->crypto_capabilities.num_crypto_cap) {
		return -EINVAL;
	}

	if (get_ufs_data_mask_for_data_unit_size(data_unit_size,
						 &data_unit_mask) != 0) {
		return -EINVAL;
	}

	if ((data_unit_mask & hba->crypto_cap_array[cap_idx].sdus_mask) == 0)
		return -EINVAL;

	memset(&cfg, 0, sizeof(cfg));
	cfg.data_unit_size = data_unit_mask;
	cfg.crypto_cap_idx = cap_idx;
	cfg.config_enable |= UFS_CRYPTO_CONFIGURATION_ENABLE;

	c = ufshcd_crypto_cfg_entry_write_key(&cfg, key,
					      hba->crypto_cap_array[cap_idx]);
	if (c != 0)
		return c;

	program_key(hba, &cfg, slot);

	memcpy(&cfg_arr[slot], &cfg, sizeof(cfg));

	return 0;
}

static int ufshcd_crypto_keyslot_find(void *hba_p,
				      const u8 *key,
				      unsigned int data_unit_size_bytes,
				      unsigned int cap_idx)
{
	struct ufs_hba *hba = hba_p;
	int c = 0;
	u8 data_unit_mask;
	union ufs_crypto_cfg_entry cfg;
	union ufs_crypto_cfg_entry *cfg_arr = hba->crypto_priv;

	if (!ufshcd_is_crypto_enabled(hba) ||
	    cap_idx >= hba->crypto_capabilities.num_crypto_cap) {
		return -EINVAL;
	}

	if (get_ufs_data_mask_for_data_unit_size(data_unit_size_bytes,
						 &data_unit_mask) != 0) {
		return -EINVAL;
	}

	if ((data_unit_mask & hba->crypto_cap_array[cap_idx].sdus_mask) == 0)
		return -EINVAL;

	memset(&cfg, 0, sizeof(cfg));
	c = ufshcd_crypto_cfg_entry_write_key(&cfg, key,
					      hba->crypto_cap_array[cap_idx]);

	if (c != 0)
		return -EINVAL;

	for (c = 0; c <= hba->crypto_capabilities.config_count; c++) {
		if ((cfg_arr[c].config_enable &
		     UFS_CRYPTO_CONFIGURATION_ENABLE) &&
		    memcmp(&cfg.crypto_key, cfg_arr[c].crypto_key,
			   UFS_CRYPTO_KEY_MAX_SIZE) == 0 &&
		    data_unit_mask == cfg_arr[c].data_unit_size &&
		    cap_idx == cfg_arr[c].crypto_cap_idx) {
			return c;
		}
	}

	return -ENOKEY;
}

static int ufshcd_crypto_keyslot_evict(void *hba_p, unsigned int slot)
{
	struct ufs_hba *hba = hba_p;
	int c = 0;
	__le32 reg_base;
	union ufs_crypto_cfg_entry *cfg;
	union ufs_crypto_cfg_entry *cfg_arr = hba->crypto_priv;

	if (!ufshcd_is_crypto_enabled(hba) ||
	    !crypto_cfg_slot_in_bounds(hba, slot)) {
		return -EINVAL;
	}

	cfg = &cfg_arr[slot];
	memset(cfg, 0, sizeof(*cfg));
	reg_base = hba->crypto_cfg_register +
			   slot * sizeof(union ufs_crypto_cfg_entry);

	/* Clear the crypto cfg on the device */
	for (c = 0; c < sizeof(*cfg); c += sizeof(__le32))
		ufshcd_writel(hba, 0, reg_base + c);
	mb();

	return 0;
}

static enum ufs_crypto_key_size get_keysize_enum(size_t key_size_bytes)
{
	switch (key_size_bytes) {
	case 16: return UFS_CRYPTO_KEY_SIZE_128;
	case 24: return UFS_CRYPTO_KEY_SIZE_192;
	case 32: return UFS_CRYPTO_KEY_SIZE_256;
	case 64: return UFS_CRYPTO_KEY_SIZE_512;
	}

	return UFS_CRYPTO_KEY_SIZE_INVALID;
}

static int ufshcd_crypto_alg_find(void *hba_p, size_t key_size_bytes,
			   enum keyslot_manager_algs alg,
			   unsigned int data_unit_size)
{
	struct ufs_hba *hba = hba_p;
	enum ufs_crypto_alg ufs_alg;
	u8 data_unit_mask;
	int c;
	enum ufs_crypto_key_size ufs_key_size;
	union ufs_crypto_cap_entry *ccap_array = hba->crypto_cap_array;

	if (!ufshcd_hba_is_crypto_supported(hba))
		return -EINVAL;

	switch (alg) {
	case AES_XTS:
		ufs_alg = UFS_CRYPTO_ALG_AES_XTS;
		break;
	case BITLOCKER_AES_CBC:
		ufs_alg = UFS_CRYPTO_ALG_BITLOCKER_AES_CBC;
		break;
	case AES_ECB:
		ufs_alg = UFS_CRYPTO_ALG_AES_ECB;
		break;
	case ESSIV_AES_CBC:
		ufs_alg = UFS_CRYPTO_ALG_ESSIV_AES_CBC;
		break;
	default: return -EINVAL;
	}

	/* AES_XTS needs two keys */
	if (ufs_alg == UFS_CRYPTO_ALG_AES_XTS) {
		key_size_bytes /= 2;
	} else if (key_size_bytes == 24) {
		/* All other encryption modes don't support 24 byte keys */
		return -EINVAL;
	}

	ufs_key_size = get_keysize_enum(key_size_bytes);
	if (ufs_key_size == UFS_CRYPTO_KEY_SIZE_INVALID)
		return -EINVAL;

	if (get_ufs_data_mask_for_data_unit_size(data_unit_size,
						 &data_unit_mask) != 0) {
		return -EINVAL;
	}

	for (c = 0; c < hba->crypto_capabilities.num_crypto_cap; c++) {
		if (ccap_array[c].algorithm_id == ufs_alg &&
		    (ccap_array[c].sdus_mask & data_unit_mask) != 0 &&
		    ccap_array[c].key_size == ufs_key_size) {
			return c;
		}
	}

	return -EINVAL;
}

/**
 * ufshcd_crypto_set_enable_slot - enables/disables a crypto config slot
 * @hba:
 * @slot: The slot to modify
 * @enable: Whether to enable or disable the slot
 *
 * Returns 0 on success, negative number on failure.
 */
int ufshcd_crypto_set_enable_slot(struct ufs_hba *hba,
				  unsigned int slot,
				  bool enable)
{
	__le32 offset, orig_value, new_value;

	if (!ufshcd_is_crypto_enabled(hba) ||
	    !crypto_cfg_slot_in_bounds(hba, slot)) {
		return -EINVAL;
	}

	offset = hba->crypto_cfg_register +
		 slot * sizeof(union ufs_crypto_cfg_entry) +
		 offsetof(union ufs_crypto_cfg_entry, config_enable);

	orig_value = ufshcd_readl(hba, offset);
	if (enable)
		new_value = orig_value | UFS_CRYPTO_CONFIGURATION_ENABLE;
	else
		new_value = orig_value & (~UFS_CRYPTO_CONFIGURATION_ENABLE);

	ufshcd_writel(hba, new_value, offset);
	mb();

	return 0;
}

void ufshcd_crypto_reset(struct ufs_hba *hba)
{
	union ufs_crypto_cfg_entry *cfg_arr = hba->crypto_priv;
	int c;

	/* Reset might clear all keys, so reprogram all the keys */
	for (c = 0; c <= hba->crypto_capabilities.config_count; c++) {
		program_key(hba, &cfg_arr[c], c);
	}
}

int ufshcd_crypto_enable(struct ufs_hba *hba, bool enable)
{
	if (!ufshcd_hba_is_crypto_supported(hba))
		return -EINVAL;

	if (enable) {
		hba->caps |= UFSHCD_CAP_CRYPTO;
		ufshcd_crypto_reset(hba);
	} else {
		hba->caps &= ~UFSHCD_CAP_CRYPTO;
	}

	return 0;
}

/**
 * ufshcd_hba_init_crypto - Inits the crypto engine if it exists.
 * @hba: Per adapter instance
 *
 * Returns 0 on success. Returns -ENODEV if such capabilties don't exist, and
 * -ENOMEM upon OOM. In either error case, the UFS driver can and should
 * continue as if crypto capabiltites are not present.
 */
int ufshcd_hba_init_crypto(struct ufs_hba *hba)
{
	int c = 0;
	int err = 0;
	/* Default to disabling crypto */
	hba->caps &= ~UFSHCD_CAP_CRYPTO;

	if (!(hba->capabilities & MASK_CRYPTO_SUPPORT)) {
		err = -ENODEV;
		goto out;
	}

	/**
	 * Crypto Capabilities should never be 0, because the
	 * config_array_ptr >= 04h. So we use a 0 value to indicate that
	 * crypto init failed, and can't be enabled.
	 */
	hba->crypto_capabilities.reg_val = ufshcd_readl(hba, REG_UFS_CCAP);
	hba->crypto_cfg_register =
		(u32)hba->crypto_capabilities.config_array_ptr * 0x100;
	hba->crypto_cap_array =
		devm_kcalloc(hba->dev,
			     hba->crypto_capabilities.num_crypto_cap,
			     sizeof(hba->crypto_cap_array[0]),
			     GFP_KERNEL);
	if (!hba->crypto_cap_array) {
		err = -ENOMEM;
		goto out;
	}

	hba->crypto_priv =
		devm_kcalloc(hba->dev,
			     (uint)hba->crypto_capabilities.config_count + 1,
			     sizeof(union ufs_crypto_cfg_entry),
			     GFP_KERNEL);
	if (!hba->crypto_priv) {
		err = -ENOMEM;
		goto out_cfg_mem;
	}

	/**
	 * Store all the capabilities now so that we don't need to repeatedly
	 * access the device each time we want to know its capabilities
	 */
	for (c = 0; c < hba->crypto_capabilities.num_crypto_cap; c++) {
		hba->crypto_cap_array[c].reg_val =
			ufshcd_readl(hba,
				     REG_UFS_CRYPTOCAP + c * sizeof(__le32));
	}

	return 0;
out_cfg_mem:
	devm_kfree(hba->dev, hba->crypto_cap_array);
out:
	// TODO: print error?
	/* Indicate that init failed by setting crypto_capabilities to 0 */
	hba->crypto_capabilities.reg_val = 0;
	return err;
}

int ufshcd_crypto_setup_rq_keyslot_manager(struct ufs_hba *hba,
					   struct request_queue *q)
{
	int err = 0;

	if (!ufshcd_hba_is_crypto_supported(hba))
		return 0;

	if (!q) {
		err = -ENODEV;
		goto out_no_q;
	}

	q->ksm = keyslot_manager_create(
	    hba->crypto_capabilities.config_count+1,
	    &ufshcd_ksm_ops, hba);
	/*
	 * If we fail we make it look like
	 * crypto is not supported, which will avoid issues
	 * with reset
	 */
	if (!q->ksm) {
		err = -ENOMEM;
out_no_q:
		ufshcd_crypto_enable(hba, false);
		hba->crypto_capabilities.reg_val = 0;
		devm_kfree(hba->dev, hba->crypto_cap_array);
		devm_kfree(hba->dev, hba->crypto_priv);
		return err;
	}

	return 0;
}

int ufshcd_crypto_destroy_rq_keyslot_manager(struct request_queue *q)
{
	if (q && q->ksm)
		keyslot_manager_destroy(q->ksm);

	return 0;
}

const struct keyslot_mgmt_ll_ops ufshcd_ksm_ops = {
	.keyslot_program	= ufshcd_crypto_keyslot_program,
	.keyslot_evict		= ufshcd_crypto_keyslot_evict,
	.keyslot_find		= ufshcd_crypto_keyslot_find,
	.crypto_alg_find	= ufshcd_crypto_alg_find,
};
