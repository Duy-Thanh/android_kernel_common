// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

#ifndef _UFSHCD_CRYPTO_H
#define _UFSHCD_CRYPTO_H

struct ufs_hba;

#ifdef CONFIG_SCSI_UFS_CRYPTO
#include <linux/keyslot-manager.h>

#include "ufshci.h"

bool crypto_cfg_slot_in_bounds(struct ufs_hba *hba, unsigned int slot);

bool ufshcd_hba_is_crypto_supported(struct ufs_hba *hba);

bool ufshcd_is_crypto_enabled(struct ufs_hba *hba);

int ufshcd_crypto_set_enable_slot(struct ufs_hba *hba,
				  unsigned int slot,
				  bool enable);

int ufshcd_crypto_enable(struct ufs_hba *hba, bool enable);

void ufshcd_crypto_reset(struct ufs_hba *hba);

int ufshcd_hba_init_crypto(struct ufs_hba *hba);

int ufshcd_crypto_setup_rq_keyslot_manager(struct ufs_hba *hba,
					   struct request_queue *q);

int ufshcd_crypto_destroy_rq_keyslot_manager(struct request_queue *q);

extern const struct keyslot_mgmt_ll_ops ufshcd_ksm_ops;

#else /* CONFIG_UFS_CRYPTO */

static inline bool crypto_cfg_slot_in_bounds(struct ufs_hba *hba,
					     unsigned int slot)
{
	return false;
}

static inline bool ufshcd_hba_is_crypto_supported(struct ufs_hba *hba)
{
	return false;
}

static inline bool ufshcd_is_crypto_enabled(struct ufs_hba *hba)
{
	return false;
}

static inline int ufshcd_crypto_set_enable_slot(struct ufs_hba *hba,
				  unsigned int slot,
				  bool enable)
{
	return -1;
}

static inline int ufshcd_crypto_enable(struct ufs_hba *hba,
				       bool enable)
{
	return -1;
}

static inline void ufshcd_crypto_reset(struct ufs_hba *hba) { }

static inline int ufshcd_hba_init_crypto(struct ufs_hba *hba)
{
	return -1;
}

static inline int ufshcd_crypto_setup_rq_keyslot_manager(
					struct ufs_hba *hba,
					struct request_queue *q)
{
	return -1;
}

static inline int ufshcd_crypto_destroy_rq_keyslot_manager(
				struct request_queue *q)
{
	return -1;
}

const struct keyslot_mgmt_ll_ops ufshcd_ksm_ops = { 0 };

#endif /* CONFIG_SCSI_UFS_CRYPTO */

#endif /* _UFSHCD_CRYPTO_H */
