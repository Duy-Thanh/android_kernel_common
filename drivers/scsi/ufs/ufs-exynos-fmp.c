/*
 * Exynos FMP UFS crypto interface
 *
 * Copyright (C) 2020 Samsung Electronics Co., Ltd.
 * Authors: Boojin Kim <boojin.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/keyslot-manager.h>
#include "ufshcd.h"
#include "ufshcd-crypto.h"
#include "ufs-exynos.h"
#include <crypto/fmp.h>

#ifdef CONFIG_SCSI_UFS_EXYNOS_FMP
enum fmp_crypto_api {
	api_init,
	api_setup_rq_keyslot_manager,
	api_destroy_rq_keyslot_manager,
	api_hba_init_crypto,
	api_suspend,
	api_resume,
	api_prepare_lrbp_crypto,
	api_prepare_lrbp_crypto_loop,
	api_complete_lrbp_crypto,
	api_lrbp_crypto_err,
	api_max,
};

static void fmp_ufshcd_crypto_setup_rq_keyslot_manager_spec(struct ufs_hba *hba,
						 struct request_queue *q)
{
	q->ksm = hba->ksm;
}

static void fmp_ufshcd_crypto_destroy_rq_keyslot_manager(struct ufs_hba *hba,
					      struct request_queue *q)
{
}

static int fmp_ufshcd_prepare_lrbp_crypto(struct ufs_hba *hba,
			       struct scsi_cmnd *cmd,
			       struct ufshcd_lrb *lrbp)
{
	return 0;
}

static int fmp_ufshcd_map_sg_crypto(struct ufs_hba *hba,
			       struct ufshcd_lrb *lrbp)
{
	struct scsi_cmnd *cmd = lrbp->cmd;
	struct bio *bio = cmd->request->bio;
	struct request_queue *q = cmd->request->q;
	int sg_segments = scsi_sg_count(lrbp->cmd);
	struct scatterlist *sg;
	struct fmp_crypto_info fmp_info;
	struct fmp_request req;
	int ret = 0;
	int idx = 0;
	u64 iv = 0;
	struct ufshcd_sg_entry *prd = (struct ufshcd_sg_entry *)lrbp->ucd_prdt_ptr;

	if (!bio || !q)
		return 0;

	if (!q->ksm || !bio_crypt_should_process(cmd->request)) {
		req.table = prd;
		req.prdt_off = hba->sg_entry_size;
		req.prdt_cnt = sg_segments;
		ret = exynos_fmp_bypass(&req, bio);
		if (ret) {
			pr_debug("%s: find fips\n", __func__);
			req.fips = true;
			goto encrypt;
		}
		return 0;
	}
	fmp_info.enc_mode = EXYNOS_FMP_FILE_ENC;
	fmp_info.algo_mode = EXYNOS_FMP_ALGO_MODE_AES_XTS;

	ret = exynos_fmp_setkey(&fmp_info,
		(u8 *)bio->bi_crypt_context->bc_key->raw,
		bio->bi_crypt_context->bc_key->size, 0);
	if (ret) {
		pr_err("%s: fails to set fmp key. ret:%d\n", __func__, ret);
		return ret;
	}

	req.iv = &iv;
	req.ivsize = sizeof(iv);
	req.fips = false;
encrypt:
	req.cmdq_enabled = 0;
	scsi_for_each_sg(lrbp->cmd, sg, sg_segments, idx) {
		if (!req.fips)
			iv = bio->bi_crypt_context->bc_dun[0] + idx;
		req.table = prd;
		ret = exynos_fmp_crypt(&fmp_info, &req);
		if (ret) {
			pr_err("%s: fails to crypt fmp key. ret:%d\n", __func__, ret);
			return ret;
		}
		prd = (void *)prd + hba->sg_entry_size;
	}
	return 0;
}

static int fmp_ufshcd_complete_lrbp_crypto(struct ufs_hba *hba,
				struct scsi_cmnd *cmd,
				struct ufshcd_lrb *lrbp)
{
	struct bio *bio = cmd->request->bio;
	struct request_queue *q = cmd->request->q;
	int sg_segments = scsi_sg_count(lrbp->cmd);
	struct scatterlist *sg;
	struct fmp_crypto_info fmp_info;
	struct fmp_request req;
	int ret = 0;
	int idx = 0;
	struct ufshcd_sg_entry *prd;

	if (!bio || !q)
		return 0;

	if (!q->ksm || !bio_crypt_should_process(cmd->request))
		return 0;

	prd = (struct ufshcd_sg_entry *)lrbp->ucd_prdt_ptr;
	scsi_for_each_sg(lrbp->cmd, sg, sg_segments, idx) {
		req.table = prd;
		ret = exynos_fmp_clear(&fmp_info, &req);
		if (ret) {
			pr_warn("%s: fails to clear fips\n", __func__);
			break;
		}
		prd = (void *)prd + hba->sg_entry_size;
	}
	return 0;
}

static int fmp_ufshcd_crypto_keyslot_program(struct keyslot_manager *ksm,
					 const struct blk_crypto_key *key,
					 unsigned int slot)
{
	return 0;
}

static int fmp_ufshcd_crypto_keyslot_evict(struct keyslot_manager *ksm,
				       const struct blk_crypto_key *key,
				       unsigned int slot)
{
	return 0;
}

static const struct keyslot_mgmt_ll_ops fmp_ksm_ops = {
	.keyslot_program	= fmp_ufshcd_crypto_keyslot_program,
	.keyslot_evict		= fmp_ufshcd_crypto_keyslot_evict,
};

static int fmp_ufshcd_hba_init_crypto_spec(struct ufs_hba *hba,
					const struct keyslot_mgmt_ll_ops *ksm_ops)
{
	unsigned int crypto_modes_supported[BLK_ENCRYPTION_MODE_MAX] = {
		[BLK_ENCRYPTION_MODE_AES_256_XTS] = 4096,
	};

	hba->ksm = keyslot_manager_create_passthrough(NULL, &fmp_ksm_ops,
					  crypto_modes_supported, NULL);
	if (!hba->ksm) {
		pr_info("%s fails to get keyslot manager\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static void fmp_ufshcd_enable(struct ufs_hba *hba)
{
}

static int fmp_ufshcd_suspend_crypto(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	return 0;
}

static int fmp_ufshcd_resume_crypto(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	exynos_fmp_sec_cfg(0, 0, 0);
	return 0;
}

static struct ufs_hba_crypto_variant_ops exynos_ufs_fmp_ops = {
	.setup_rq_keyslot_manager = fmp_ufshcd_crypto_setup_rq_keyslot_manager_spec,
	.destroy_rq_keyslot_manager = fmp_ufshcd_crypto_destroy_rq_keyslot_manager,
	.hba_init_crypto = fmp_ufshcd_hba_init_crypto_spec,
	.enable = fmp_ufshcd_enable,
	.disable = fmp_ufshcd_enable,
	.suspend = fmp_ufshcd_suspend_crypto,
	.resume = fmp_ufshcd_resume_crypto,
	.debug = NULL,
	.map_sg_crypto = fmp_ufshcd_map_sg_crypto,
	.prepare_lrbp_crypto = fmp_ufshcd_prepare_lrbp_crypto,
	.complete_lrbp_crypto = fmp_ufshcd_complete_lrbp_crypto,
};

void exynos_ufs_fmp_config(struct ufs_hba *hba)
{
	hba->sg_entry_size = sizeof(struct fmp_table_setting);
	hba->crypto_vops = &exynos_ufs_fmp_ops;
	exynos_fmp_sec_cfg(0, 0, 1);
}
#else
void exynos_ufs_fmp_config(struct ufs_hba *hba)
{
}
#endif
