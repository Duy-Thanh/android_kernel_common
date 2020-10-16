// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Universal Flash Storage Host Performance Booster
 *
 * Copyright (C) 2017-2018 Samsung Electronics Co., Ltd.
 *
 * Authors:
 *	Yongmyung Lee <ymhungry.lee@samsung.com>
 *	Jinyoung Choi <j-young.choi@samsung.com>
 */

#include <asm/unaligned.h>
#include <linux/async.h>

#include "ufshcd.h"
#include "ufshpb.h"
#include "../sd.h"

bool ufshpb_is_allowed(struct ufs_hba *hba)
{
	return !(hba->ufshpb_dev.hpb_disabled);
}

static int ufshpb_get_state(struct ufshpb_lu *hpb)
{
	return atomic_read(&hpb->hpb_state);
}

static void ufshpb_set_state(struct ufshpb_lu *hpb, int state)
{
	atomic_set(&hpb->hpb_state, state);
}

void ufshpb_prep(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
}

void ufshpb_rsp_upiu(struct ufs_hba *hba, struct ufshcd_lrb *lrbp)
{
}

static void ufshpb_init_subregion_tbl(struct ufshpb_lu *hpb,
				      struct ufshpb_region *rgn)
{
	int srgn_idx;

	for (srgn_idx = 0; srgn_idx < rgn->srgn_cnt; srgn_idx++) {
		struct ufshpb_subregion *srgn = rgn->srgn_tbl + srgn_idx;

		srgn->rgn_idx = rgn->rgn_idx;
		srgn->srgn_idx = srgn_idx;
		srgn->srgn_state = HPB_SRGN_UNUSED;
	}
}

static int ufshpb_alloc_subregion_tbl(struct ufshpb_lu *hpb,
					     struct ufshpb_region *rgn,
					     int srgn_cnt)
{
	rgn->srgn_tbl = kvcalloc(srgn_cnt, sizeof(struct ufshpb_subregion),
				 GFP_KERNEL);
	if (!rgn->srgn_tbl)
		return -ENOMEM;

	rgn->srgn_cnt = srgn_cnt;
	return 0;
}

static void ufshpb_init_lu_parameter(struct ufs_hba *hba,
				     struct ufshpb_lu *hpb,
				     struct ufshpb_dev_info *hpb_dev_info,
				     struct ufshpb_lu_info *hpb_lu_info)
{
	u32 entries_per_rgn;
	u64 rgn_mem_size, tmp;

	hpb->lu_pinned_start = hpb_lu_info->pinned_start;
	hpb->lu_pinned_end = hpb_lu_info->num_pinned ?
		(hpb_lu_info->pinned_start + hpb_lu_info->num_pinned - 1)
		: PINNED_NOT_SET;

	rgn_mem_size = (1ULL << hpb_dev_info->rgn_size) * HPB_RGN_SIZE_UNIT
			* HPB_ENTRY_SIZE;
	do_div(rgn_mem_size, HPB_ENTRY_BLOCK_SIZE);
	hpb->srgn_mem_size = (1ULL << hpb_dev_info->srgn_size)
		* HPB_RGN_SIZE_UNIT / HPB_ENTRY_BLOCK_SIZE * HPB_ENTRY_SIZE;

	tmp = rgn_mem_size;
	do_div(tmp, HPB_ENTRY_SIZE);
	entries_per_rgn = (u32)tmp;
	hpb->entries_per_rgn_shift = ilog2(entries_per_rgn);
	hpb->entries_per_rgn_mask = entries_per_rgn - 1;

	hpb->entries_per_srgn = hpb->srgn_mem_size / HPB_ENTRY_SIZE;
	hpb->entries_per_srgn_shift = ilog2(hpb->entries_per_srgn);
	hpb->entries_per_srgn_mask = hpb->entries_per_srgn - 1;

	tmp = rgn_mem_size;
	do_div(tmp, hpb->srgn_mem_size);
	hpb->srgns_per_rgn = (int)tmp;

	hpb->rgns_per_lu = DIV_ROUND_UP(hpb_lu_info->num_blocks,
				entries_per_rgn);
	hpb->srgns_per_lu = DIV_ROUND_UP(hpb_lu_info->num_blocks,
				(hpb->srgn_mem_size / HPB_ENTRY_SIZE));

	hpb->pages_per_srgn = hpb->srgn_mem_size / PAGE_SIZE;

	dev_info(hba->dev, "ufshpb(%d): region memory size - %llu (bytes)\n",
		 hpb->lun, rgn_mem_size);
	dev_info(hba->dev, "ufshpb(%d): subregion memory size - %u (bytes)\n",
		 hpb->lun, hpb->srgn_mem_size);
	dev_info(hba->dev, "ufshpb(%d): total blocks per lu - %d\n",
		 hpb->lun, hpb_lu_info->num_blocks);
	dev_info(hba->dev, "ufshpb(%d): subregions per region - %d, regions per lu - %u",
		 hpb->lun, hpb->srgns_per_rgn, hpb->rgns_per_lu);
}

static int ufshpb_alloc_region_tbl(struct ufs_hba *hba, struct ufshpb_lu *hpb)
{
	struct ufshpb_region *rgn_table, *rgn;
	int rgn_idx, i;
	int ret = 0;

	rgn_table = kvcalloc(hpb->rgns_per_lu, sizeof(struct ufshpb_region),
			    GFP_KERNEL);
	if (!rgn_table)
		return -ENOMEM;

	hpb->rgn_tbl = rgn_table;

	for (rgn_idx = 0; rgn_idx < hpb->rgns_per_lu; rgn_idx++) {
		int srgn_cnt = hpb->srgns_per_rgn;

		rgn = rgn_table + rgn_idx;
		rgn->rgn_idx = rgn_idx;

		if (rgn_idx == hpb->rgns_per_lu - 1)
			srgn_cnt = ((hpb->srgns_per_lu - 1) %
				    hpb->srgns_per_rgn) + 1;

		ret = ufshpb_alloc_subregion_tbl(hpb, rgn, srgn_cnt);
		if (ret)
			goto release_srgn_table;
		ufshpb_init_subregion_tbl(hpb, rgn);

		rgn->rgn_state = HPB_RGN_INACTIVE;
	}

	return 0;

release_srgn_table:
	for (i = 0; i < rgn_idx; i++) {
		rgn = rgn_table + i;
		if (rgn->srgn_tbl)
			kvfree(rgn->srgn_tbl);
	}
	kvfree(rgn_table);
	return ret;
}

static void ufshpb_destroy_subregion_tbl(struct ufshpb_lu *hpb,
					 struct ufshpb_region *rgn)
{
	int srgn_idx;

	for (srgn_idx = 0; srgn_idx < rgn->srgn_cnt; srgn_idx++) {
		struct ufshpb_subregion *srgn;

		srgn = rgn->srgn_tbl + srgn_idx;
		srgn->srgn_state = HPB_SRGN_UNUSED;
	}
}

static void ufshpb_destroy_region_tbl(struct ufshpb_lu *hpb)
{
	int rgn_idx;

	for (rgn_idx = 0; rgn_idx < hpb->rgns_per_lu; rgn_idx++) {
		struct ufshpb_region *rgn;

		rgn = hpb->rgn_tbl + rgn_idx;
		if (rgn->rgn_state != HPB_RGN_INACTIVE) {
			rgn->rgn_state = HPB_RGN_INACTIVE;

			ufshpb_destroy_subregion_tbl(hpb, rgn);
		}

		kvfree(rgn->srgn_tbl);
	}

	kvfree(hpb->rgn_tbl);
}

/* SYSFS functions */
#define ufshpb_sysfs_attr_show_func(__name)				\
static ssize_t __name##_show(struct device *dev,			\
	struct device_attribute *attr, char *buf)			\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	struct ufshpb_lu *hpb = sdev->hostdata;				\
									\
	if (!hpb)							\
		return -ENOENT;						\
	return snprintf(buf, PAGE_SIZE, "%d\n",				\
			atomic_read(&hpb->stats.__name));		\
}									\
\
static DEVICE_ATTR_RO(__name)

ufshpb_sysfs_attr_show_func(hit_cnt);
ufshpb_sysfs_attr_show_func(miss_cnt);
ufshpb_sysfs_attr_show_func(rb_noti_cnt);
ufshpb_sysfs_attr_show_func(rb_active_cnt);
ufshpb_sysfs_attr_show_func(rb_inactive_cnt);
ufshpb_sysfs_attr_show_func(map_req_cnt);

static struct attribute *hpb_dev_attrs[] = {
	&dev_attr_hit_cnt.attr,
	&dev_attr_miss_cnt.attr,
	&dev_attr_rb_noti_cnt.attr,
	&dev_attr_rb_active_cnt.attr,
	&dev_attr_rb_inactive_cnt.attr,
	&dev_attr_map_req_cnt.attr,
	NULL,
};

struct attribute_group ufs_sysfs_hpb_stat_group = {
	.name = "hpb_sysfs",
	.attrs = hpb_dev_attrs,
};

static void ufshpb_stat_init(struct ufshpb_lu *hpb)
{
	atomic_set(&hpb->stats.hit_cnt, 0);
	atomic_set(&hpb->stats.miss_cnt, 0);
	atomic_set(&hpb->stats.rb_noti_cnt, 0);
	atomic_set(&hpb->stats.rb_active_cnt, 0);
	atomic_set(&hpb->stats.rb_inactive_cnt, 0);
	atomic_set(&hpb->stats.map_req_cnt, 0);
}

static int ufshpb_lu_hpb_init(struct ufs_hba *hba, struct ufshpb_lu *hpb,
			      struct ufshpb_dev_info *hpb_dev_info)
{
	int ret;

	spin_lock_init(&hpb->hpb_state_lock);

	ret = ufshpb_alloc_region_tbl(hba, hpb);

	ufshpb_stat_init(hpb);

	return 0;
}

static struct ufshpb_lu *ufshpb_alloc_hpb_lu(struct ufs_hba *hba, int lun,
				     struct ufshpb_dev_info *hpb_dev_info,
				     struct ufshpb_lu_info *hpb_lu_info)
{
	struct ufshpb_lu *hpb;
	int ret;

	hpb = kzalloc(sizeof(struct ufshpb_lu), GFP_KERNEL);
	if (!hpb)
		return NULL;

	hpb->lun = lun;

	ufshpb_init_lu_parameter(hba, hpb, hpb_dev_info, hpb_lu_info);

	ret = ufshpb_lu_hpb_init(hba, hpb, hpb_dev_info);
	if (ret) {
		dev_err(hba->dev, "hpb lu init failed. ret %d", ret);
		goto release_hpb;
	}

	return hpb;

release_hpb:
	kfree(hpb);
	return NULL;
}

static void ufshpb_issue_hpb_reset_query(struct ufs_hba *hba)
{
	int err = 0;
	int retries;

	for (retries = 0; retries < HPB_RESET_REQ_RETRIES; retries++) {
		err = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_SET_FLAG,
				QUERY_FLAG_IDN_HPB_RESET, 0, NULL);
		if (err)
			dev_dbg(hba->dev,
				"%s: failed with error %d, retries %d\n",
				__func__, err, retries);
		else
			return;
	}

	if (err)
		dev_err(hba->dev,
			"%s setting fHpbReset flag failed with error %d\n",
			__func__, err);
}

static void ufshpb_check_hpb_reset_query(struct ufs_hba *hba)
{
	int err = 0;
	bool flag_res = true;
	int try;

	/* wait for the device to complete HPB reset query */
	for (try = 0; try < HPB_RESET_REQ_RETRIES; try++) {
		dev_info(hba->dev,
			"%s start flag reset polling %d times\n",
			__func__, try);

		/* Poll fHpbReset flag to be cleared */
		err = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_READ_FLAG,
				QUERY_FLAG_IDN_HPB_RESET, 0, &flag_res);

		if (err) {
			dev_err(hba->dev,
				"%s reading fHpbReset flag failed with error %d\n",
				__func__, err);
			return;
		}

		usleep_range(1000, 1100);

		if (!flag_res)
			return;
	}
	if (flag_res) {
		dev_err(hba->dev,
			"%s fHpbReset was not cleared by the device\n",
			__func__);
	}
}

void ufshpb_reset(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		if (ufshpb_get_state(hpb) != HPB_RESET)
			continue;

		ufshpb_set_state(hpb, HPB_PRESENT);
	}
}

void ufshpb_reset_host(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	dev_info(hba->dev, "ufshpb run reset_host");
	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		if (ufshpb_get_state(hpb) != HPB_PRESENT)
			continue;
		ufshpb_set_state(hpb, HPB_RESET);
	}
}

void ufshpb_suspend(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	dev_info(hba->dev, "ufshpb goto suspend");

	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		if (ufshpb_get_state(hpb) != HPB_PRESENT)
			continue;
		ufshpb_set_state(hpb, HPB_SUSPEND);
	}
}

void ufshpb_resume(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	dev_info(hba->dev, "ufshpb resume");

	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		if ((ufshpb_get_state(hpb) != HPB_PRESENT) &&
		    (ufshpb_get_state(hpb) != HPB_SUSPEND))
			continue;
		ufshpb_set_state(hpb, HPB_PRESENT);
	}
}

static int ufshpb_read_desc(struct ufs_hba *hba, u8 desc_id, u8 desc_index,
			    u8 selector, u8 *desc_buf)
{
	int err = 0;
	int size;

	ufshcd_map_desc_id_to_length(hba, desc_id, &size);

	pm_runtime_get_sync(hba->dev);

	err = ufshcd_query_descriptor_retry(hba, UPIU_QUERY_OPCODE_READ_DESC,
					    desc_id, desc_index,
					    selector,
					    desc_buf, &size);
	if (err)
		dev_err(hba->dev, "read desc failed: %d, id %d, idx %d\n",
			err, desc_id, desc_index);

	pm_runtime_put_sync(hba->dev);

	return err;
}

static int ufshpb_get_lu_info(struct ufs_hba *hba, int lun,
				    struct ufshpb_lu_info *hpb_lu_info,
				    u8 *desc_buf)
{
	u16 max_active_rgns;
	u8 lu_enable;
	int ret;

	ret = ufshpb_read_desc(hba, QUERY_DESC_IDN_UNIT, lun, 0, desc_buf);
	if (ret) {
		dev_err(hba->dev,
			"%s: idn: %d lun: %d  query request failed",
			__func__, QUERY_DESC_IDN_UNIT, lun);
		return ret;
	}

	lu_enable = desc_buf[UNIT_DESC_PARAM_LU_ENABLE];
	if (lu_enable != LU_ENABLED_HPB_FUNC)
		return -ENODEV;

	max_active_rgns = get_unaligned_be16(
			desc_buf + UNIT_DESC_HPB_LU_MAX_ACTIVE_REGIONS);
	if (!max_active_rgns) {
		dev_err(hba->dev,
			"lun %d wrong number of max active regions\n", lun);
		return -ENODEV;
	}

	hpb_lu_info->num_blocks = get_unaligned_be64(
			desc_buf + UNIT_DESC_PARAM_LOGICAL_BLK_COUNT);
	hpb_lu_info->pinned_start = get_unaligned_be16(
			desc_buf + UNIT_DESC_HPB_LU_PIN_REGION_START_OFFSET);
	hpb_lu_info->num_pinned = get_unaligned_be16(
			desc_buf + UNIT_DESC_HPB_LU_NUM_PIN_REGIONS);
	hpb_lu_info->max_active_rgns = max_active_rgns;

	return 0;
}

static void ufshpb_hpb_lu_prepared(struct ufs_hba *hba)
{
	struct ufshpb_lu *hpb;
	struct scsi_device *sdev;

	ufshpb_check_hpb_reset_query(hba);

	shost_for_each_device(sdev, hba->host) {
		hpb = sdev->hostdata;
		if (!hpb)
			continue;

		dev_info(hba->dev, "set state to present\n");
		ufshpb_set_state(hpb, HPB_PRESENT);
	}
}

void ufshpb_init_hpb_lu(struct ufs_hba *hba, struct scsi_device *sdev)
{
	struct ufshpb_lu *hpb;
	int ret;
	struct ufshpb_lu_info hpb_lu_info = { 0 };
	int lun = sdev->lun;
	char *desc_buf;

	desc_buf = kzalloc(QUERY_DESC_MAX_SIZE, GFP_KERNEL);
	if (!desc_buf)
		return;

	if (lun >= hba->dev_info.max_lu_supported)
		goto release_desc_buf;

	ret = ufshpb_get_lu_info(hba, lun, &hpb_lu_info, desc_buf);
	if (ret)
		goto release_desc_buf;

	hpb = ufshpb_alloc_hpb_lu(hba, lun, &hba->ufshpb_dev,
				  &hpb_lu_info);
	if (!hpb)
		goto release_desc_buf;

	hpb->sdev_ufs_lu = sdev;
	sdev->hostdata = hpb;

release_desc_buf:
	kfree(desc_buf);

	/* All LUs are initialized */
	if (atomic_dec_and_test(&hba->ufshpb_dev.slave_conf_cnt))
		ufshpb_hpb_lu_prepared(hba);
}

void ufshpb_get_geo_info(struct ufs_hba *hba, u8 *geo_buf)
{
	struct ufshpb_dev_info *hpb_dev_info = &hba->ufshpb_dev;
	int hpb_device_max_active_rgns = 0;
	int hpb_num_lu;

	hpb_dev_info->hpb_disabled = false;

	hpb_num_lu = geo_buf[GEOMETRY_DESC_PARAM_HPB_NUMBER_LU];
	if (hpb_num_lu == 0) {
		dev_err(hba->dev, "No HPB LU supported\n");
		hpb_dev_info->hpb_disabled = true;
		return;
	}

	hpb_dev_info->rgn_size = geo_buf[GEOMETRY_DESC_PARAM_HPB_REGION_SIZE];
	hpb_dev_info->srgn_size = geo_buf[GEOMETRY_DESC_PARAM_HPB_SUBREGION_SIZE];
	hpb_device_max_active_rgns =
		get_unaligned_be16(geo_buf +
			GEOMETRY_DESC_PARAM_HPB_MAX_ACTIVE_REGS);

	if (hpb_dev_info->rgn_size == 0 || hpb_dev_info->srgn_size == 0 ||
	    hpb_device_max_active_rgns == 0) {
		dev_err(hba->dev, "No HPB supported device\n");
		hpb_dev_info->hpb_disabled = true;
		return;
	}
}

void ufshpb_get_dev_info(struct ufs_hba *hba, u8 *desc_buf)
{
	struct ufshpb_dev_info *hpb_dev_info = &hba->ufshpb_dev;
	int version;
	u8 hpb_mode;

	hpb_mode = desc_buf[DEVICE_DESC_PARAM_HPB_CONTROL];
	if (hpb_mode == HPB_HOST_CONTROL) {
		dev_err(hba->dev, "%s: host control mode is not supported.\n",
			__func__);
		hpb_dev_info->hpb_disabled = true;
		return;
	}

	version = get_unaligned_be16(desc_buf + DEVICE_DESC_PARAM_HPB_VER);
	if (version != HPB_SUPPORT_VERSION) {
		dev_err(hba->dev, "%s: HPB %x version is not supported.\n",
			__func__, version);
		hpb_dev_info->hpb_disabled = true;
		return;
	}

	/*
	 * Get the number of user logical unit to check whether all
	 * scsi_device finish initialization
	 */
	hpb_dev_info->num_lu = desc_buf[DEVICE_DESC_PARAM_NUM_LU];
}

void ufshpb_init(struct ufs_hba *hba)
{
	struct ufshpb_dev_info *hpb_dev_info = &hba->ufshpb_dev;

	if (!ufshpb_is_allowed(hba))
		return;

	atomic_set(&hpb_dev_info->slave_conf_cnt, hpb_dev_info->num_lu);
	ufshpb_issue_hpb_reset_query(hba);
}

void ufshpb_destroy_lu(struct ufs_hba *hba, struct scsi_device *sdev)
{
	struct ufshpb_lu *hpb = sdev->hostdata;

	if (!hpb)
		return;

	ufshpb_set_state(hpb, HPB_FAILED);

	sdev = hpb->sdev_ufs_lu;
	sdev->hostdata = NULL;

	ufshpb_destroy_region_tbl(hpb);

	list_del_init(&hpb->list_hpb_lu);

	kfree(hpb);
}

void ufshpb_remove(struct ufs_hba *hba)
{
}

MODULE_AUTHOR("Yongmyong Lee <ymhungry.lee@samsung.com>");
MODULE_AUTHOR("Jinyoung Choi <j-young.choi@samsung.com>");
MODULE_DESCRIPTION("UFS Host Performance Booster Driver");
