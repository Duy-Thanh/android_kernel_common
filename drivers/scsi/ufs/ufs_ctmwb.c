#include "ufshcd.h"
#include "ufshci.h"
#include "ufs_ctmwb.h"

static struct ufshba_ctmwb hba_ctmwb;

/* Query request retries */
#define QUERY_REQ_RETRIES 3

static int ufshcd_query_attr_retry(struct ufs_hba *hba,
	enum query_opcode opcode, enum attr_idn idn, u8 index, u8 selector,
	u32 *attr_val)
{
	int ret = 0;
	u32 retries;

	 for (retries = QUERY_REQ_RETRIES; retries > 0; retries--) {
		ret = ufshcd_query_attr(hba, opcode, idn, index,
						selector, attr_val);
		if (ret)
			dev_dbg(hba->dev, "%s: failed with error %d, retries %d\n",
				__func__, ret, retries);
		else
			break;
	}

	if (ret)
		dev_err(hba->dev,
			"%s: query attribute, idn %d, failed with error %d after %d retires\n",
			__func__, idn, ret, QUERY_REQ_RETRIES);
	return ret;
}

static int ufshcd_query_flag_retry(struct ufs_hba *hba,
	enum query_opcode opcode, enum flag_idn idn, bool *flag_res)
{
	int ret;
	int retries;

	for (retries = 0; retries < QUERY_REQ_RETRIES; retries++) {
		ret = ufshcd_query_flag(hba, opcode, idn, flag_res);
		if (ret)
			dev_dbg(hba->dev,
				"%s: failed with error %d, retries %d\n",
				__func__, ret, retries);
		else
			break;
	}

	if (ret)
		dev_err(hba->dev,
			"%s: query attribute, opcode %d, idn %d, failed with error %d after %d retries\n",
			__func__, (int)opcode, (int)idn, ret, retries);
	return ret;
}

static int ufshcd_reset_ctmwb(struct ufs_hba *hba, bool force)
{
	int err = 0;

	if (!ufshcd_is_wb_allowed(hba))
		return 0;

	if (ufshcd_is_ctmwb_off(hba_ctmwb)) {
		dev_info(hba->dev, "%s: write booster already disabled. ctmwb_state = %d\n",
			__func__, hba_ctmwb.ufs_ctmwb_state);
		return 0;
	}

	if (ufshcd_is_ctmwb_err(hba_ctmwb))
		dev_err(hba->dev, "%s: previous write booster control was failed.\n",
			__func__);

	if (force)
		err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG,
				QUERY_FLAG_IDN_WB_EN, NULL);

	if (err) {
		ufshcd_set_ctmwb_err(hba_ctmwb);
		dev_err(hba->dev, "%s: disable write booster failed. err = %d\n",
			__func__, err);
	} else {
		ufshcd_set_ctmwb_off(hba_ctmwb);
		dev_info(hba->dev, "%s: ufs write booster disabled \n", __func__);
	}

	return 0;
}

static int ufshcd_get_ctmwb_buf_status(struct ufs_hba *hba, u32 *status)
{
	return ufshcd_query_attr_retry(hba, UPIU_QUERY_OPCODE_READ_ATTR,
			QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE, 0, 0, status);
}

static int ufshcd_ctmwb_manual_flush_ctrl(struct ufs_hba *hba, int en)
{
	int err = 0;

	dev_info(hba->dev, "%s: %sable write booster manual flush\n",
				__func__, en ? "en" : "dis");
	if (en) {
		err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG,
					QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN, NULL);
		if (err)
			dev_err(hba->dev, "%s: enable write booster failed. err = %d\n",
				__func__, err);
	} else {
		err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG,
					QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN, NULL);
		if (err)
			dev_err(hba->dev, "%s: disable write booster failed. err = %d\n",
				__func__, err);
	}

	return err;
}

static int ufshcd_ctmwb_flush_ctrl(struct ufs_hba *hba)
{
	int err = 0;
	u32 curr_status = 0;

	err = ufshcd_get_ctmwb_buf_status(hba, &curr_status);

	if (!err && (curr_status <= UFS_WB_MANUAL_FLUSH_THRESHOLD)) {
		dev_info(hba->dev, "%s: enable ctmwb manual flush, buf status : %d\n",
				__func__, curr_status);
		scsi_block_requests(hba->host);
		err = ufshcd_ctmwb_manual_flush_ctrl(hba, 1);
		if (!err) {
			mdelay(100);
			err = ufshcd_ctmwb_manual_flush_ctrl(hba, 0);
			if (err)
				dev_err(hba->dev, "%s: disable ctmwb manual flush failed. err = %d\n",
						__func__, err);
		} else
			dev_err(hba->dev, "%s: enable ctmwb manual flush failed. err = %d\n",
					__func__, err);
		scsi_unblock_requests(hba->host);
	}
	return err;
}

static int ufshcd_ctmwb_ctrl(struct ufs_hba *hba, bool enable)
{
	int err;

	if (!ufshcd_is_wb_allowed(hba))
		return 0;

	if (hba->pm_op_in_progress) {
		dev_err(hba->dev, "%s: ctmwb ctrl during pm operation is not allowed.\n",
			__func__);
		return 0;
	}

	if (ufshcd_is_ctmwb_err(hba_ctmwb))
		dev_err(hba->dev, "%s: previous write booster control was failed.\n",
			__func__);
	if (enable) {
		if (ufshcd_is_ctmwb_on(hba_ctmwb)) {
			dev_err(hba->dev, "%s: write booster already enabled. ctmwb_state = %d\n",
				__func__, hba_ctmwb.ufs_ctmwb_state);
			return 0;
		}
		pm_runtime_get_sync(hba->dev);
		err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_SET_FLAG,
					QUERY_FLAG_IDN_WB_EN, NULL);
		if (err) {
			ufshcd_set_ctmwb_err(hba_ctmwb);
			dev_err(hba->dev, "%s: enable write booster failed. err = %d\n",
				__func__, err);
		} else {
			ufshcd_set_ctmwb_on(hba_ctmwb);
			dev_info(hba->dev, "%s: ufs write booster enabled \n", __func__);
		}
	} else {
		if (ufshcd_is_ctmwb_off(hba_ctmwb)) {
			dev_err(hba->dev, "%s: write booster already disabled. ctmwb_state = %d\n",
				__func__, hba_ctmwb.ufs_ctmwb_state);
			return 0;
		}
		pm_runtime_get_sync(hba->dev);
		err = ufshcd_query_flag_retry(hba, UPIU_QUERY_OPCODE_CLEAR_FLAG,
					QUERY_FLAG_IDN_WB_EN, NULL);
		if (err) {
			ufshcd_set_ctmwb_err(hba_ctmwb);
			dev_err(hba->dev, "%s: disable write booster failed. err = %d\n",
				__func__, err);
		} else {
			ufshcd_set_ctmwb_off(hba_ctmwb);
			dev_info(hba->dev, "%s: ufs write booster disabled \n", __func__);
		}
	}

	pm_runtime_put_sync(hba->dev);

	return 0;
}

/**
 * ufshcd_get_ctmwbbuf_unit - get ctmwb buffer alloc units
 * @sdev: pointer to SCSI device
 *
 * Read dLUNumTurboWriteBufferAllocUnits in UNIT Descriptor
 * to check if LU supports write booster feature
 */
static int ufshcd_get_ctmwbbuf_unit(struct ufs_hba *hba)
{
	struct scsi_device *sdev = hba->sdev_ufs_device;
	struct ufshba_ctmwb *hba_ctmwb = (struct ufshba_ctmwb *)hba->wb_ops;
	int ret = 0;

	u32 dLUNumTurboWriteBufferAllocUnits = 0;
	u8 desc_buf[4];

	if (!ufshcd_is_wb_allowed(hba))
		return 0;

	ret = ufshcd_read_unit_desc_param(hba,
			ufshcd_scsi_to_upiu_lun(sdev->lun),
			UNIT_DESC_PARAM_WB_BUF_ALLOC_UNITS,
			desc_buf,
			sizeof(dLUNumTurboWriteBufferAllocUnits));

	/* Some WLUN doesn't support unit descriptor */
	if ((ret == -EOPNOTSUPP) || scsi_is_wlun(sdev->lun)){
		hba_ctmwb->support_ctmwb_lu = false;
		dev_info(hba->dev,"%s: do not support WB\n", __func__);
		return 0;
	}

	dLUNumTurboWriteBufferAllocUnits = ((desc_buf[0] << 24)|
			(desc_buf[1] << 16) |
			(desc_buf[2] << 8) |
			desc_buf[3]);

	if (dLUNumTurboWriteBufferAllocUnits) {
		hba_ctmwb->support_ctmwb_lu = true;
		dev_info(hba->dev, "%s: LU %d supports ctmwb, ctmwbbuf unit : 0x%x\n",
				__func__, (int)sdev->lun, dLUNumTurboWriteBufferAllocUnits);
	} else
		hba_ctmwb->support_ctmwb_lu = false;

	return 0;
}

static inline int ufshcd_ctmwb_toggle_flush(struct ufs_hba *hba, enum ufs_pm_op pm_op)
{
	ufshcd_ctmwb_flush_ctrl(hba);

	if (ufshcd_is_system_pm(pm_op))
		ufshcd_reset_ctmwb(hba, true);

	return 0;
}

static struct ufs_wb_ops exynos_ctmwb_ops = {
	.wb_toggle_flush_vendor = ufshcd_ctmwb_toggle_flush,
	.wb_alloc_units_vendor = ufshcd_get_ctmwbbuf_unit,
	.wb_ctrl_vendor = ufshcd_ctmwb_ctrl,
	.wb_reset_vendor = ufshcd_reset_ctmwb,
};

struct ufs_wb_ops *ufshcd_ctmwb_init(void)
{
	return &exynos_ctmwb_ops;
}
EXPORT_SYMBOL_GPL(ufshcd_ctmwb_init);
