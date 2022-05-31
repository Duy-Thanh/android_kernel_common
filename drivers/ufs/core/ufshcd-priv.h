/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _UFSHCD_PRIV_H_
#define _UFSHCD_PRIV_H_

#include <linux/pm_runtime.h>
#include <ufs/ufshcd.h>

/**
 * struct ufs_hba_priv - Host controller data private to the UFS core driver
 * @hba: HBA information shared between UFS core and UFS drivers.
 * @ucdl_base_addr: UFS Command Descriptor base address
 * @utrdl_base_addr: UTP Transfer Request Descriptor base address
 * @utmrdl_base_addr: UTP Task Management Descriptor base address
 * @ucdl_dma_addr: UFS Command Descriptor DMA address
 * @utrdl_dma_addr: UTRDL DMA address
 * @utmrdl_dma_addr: UTMRDL DMA address
 * @host: Scsi_Host instance of the driver
 * @dev: device handle
 * @ufs_device_wlun: WLUN that controls the entire UFS device.
 * @hwmon_device: device instance registered with the hwmon core.
 * @pm_op_in_progress: whether or not a PM operation is in progress.
 * @lrb: local reference block
 * @outstanding_tasks: Bits representing outstanding task requests
 * @outstanding_lock: Protects @outstanding_reqs.
 * @outstanding_reqs: Bits representing outstanding transfer requests
 * @reserved_slot: Used to submit device commands. Protected by @dev_cmd.lock.
 * @dev_ref_clk_freq: reference clock frequency
 * @tmf_tag_set: TMF tag set.
 * @tmf_queue: Used to allocate TMF tags.
 * @tmf_rqs: array with pointers to TMF requests while these are in progress.
 * @active_uic_cmd: handle of active UIC command
 * @uic_cmd_mutex: mutex for UIC command
 * @uic_async_done: completion used during UIC processing
 * @ufshcd_state: UFSHCD state
 * @eh_flags: Error handling flags
 * @intr_mask: Interrupt Mask Bits
 * @ee_ctrl_mask: Exception event control mask
 * @ee_drv_mask: Exception event mask for driver
 * @ee_usr_mask: Exception event mask for user (set via debugfs)
 * @ee_ctrl_mutex: Used to serialize exception event information.
 * @is_powered: flag to check if HBA is powered
 * @shutting_down: flag to check if shutdown has been invoked
 * @host_sem: semaphore used to serialize concurrent contexts
 * @eh_wq: Workqueue that eh_work works on
 * @eh_work: Worker to handle UFS errors that require s/w attention
 * @eeh_work: Worker to handle exception events
 * @errors: HBA errors
 * @uic_error: UFS interconnect layer error status
 * @saved_err: sticky error mask
 * @saved_uic_err: sticky UIC error mask
 * @ufs_stats: various error counters
 * @force_reset: flag to force eh_work perform a full reset
 * @force_pmc: flag to force a power mode change
 * @silence_err_logs: flag to silence error logs
 * @dev_cmd: ufs device management command information
 * @last_dme_cmd_tstamp: time stamp of the last completed DME command
 * @auto_bkops_enabled: to track whether bkops is enabled in device
 * @clk_gating: information related to clock gating
 * @devfreq: frequency scaling information owned by the devfreq core
 * @clk_scaling: frequency scaling information owned by the UFS driver
 * @is_sys_suspended: whether or not the entire system has been suspended
 * @urgent_bkops_lvl: keeps track of urgent bkops level for device
 * @is_urgent_bkops_lvl_checked: keeps track if the urgent bkops level for
 *  device is known or not.
 * @clk_scaling_lock: used to serialize device commands and clock scaling
 * @desc_size: descriptor sizes reported by device
 * @scsi_block_reqs_cnt: reference counting for scsi block requests
 * @bsg_dev: struct device associated with the BSG queue
 * @bsg_queue: BSG queue associated with the UFS controller
 * @rpm_dev_flush_recheck_work: used to suspend from RPM (runtime power
 *	management) after the UFS device has finished a WriteBooster buffer
 *	flush or auto BKOP.
 * @hpb_dev: information related to HPB (Host Performance Booster).
 * @monitor: statistics about UFS commands
 * @crypto_capabilities: Content of crypto capabilities register (0x100)
 * @crypto_cfg_register: Start of the crypto cfg array
 * @crypto_profile: the crypto profile of this hba (if applicable)
 * @debugfs_root: UFS controller debugfs root directory
 * @debugfs_ee_work: used to restore ee_ctrl_mask after a delay
 * @debugfs_ee_rate_limit_ms: user configurable delay after which to restore
 *	ee_ctrl_mask
 * @luns_avail: number of regular and well known LUNs supported by the UFS
 *	device
 * @complete_put: whether or not to call ufshcd_rpm_put() from inside
 *	ufshcd_resume_complete()
 */
struct ufs_hba_priv {
	struct ufs_hba hba;

	/* Virtual memory reference */
	struct utp_transfer_cmd_desc *ucdl_base_addr;
	struct utp_transfer_req_desc *utrdl_base_addr;
	struct utp_task_req_desc *utmrdl_base_addr;

	/* DMA memory reference */
	dma_addr_t ucdl_dma_addr;
	dma_addr_t utrdl_dma_addr;
	dma_addr_t utmrdl_dma_addr;

	struct Scsi_Host *host;
	struct scsi_device *ufs_device_wlun;

#ifdef CONFIG_SCSI_UFS_HWMON
	struct device *hwmon_device;
#endif

	int pm_op_in_progress;

	struct ufshcd_lrb *lrb;

	unsigned long outstanding_tasks;
	spinlock_t outstanding_lock;
	unsigned long outstanding_reqs;

	u32 reserved_slot;

	enum ufs_ref_clk_freq dev_ref_clk_freq;

	struct blk_mq_tag_set tmf_tag_set;
	struct request_queue *tmf_queue;
	struct request **tmf_rqs;

	struct uic_command *active_uic_cmd;
	struct mutex uic_cmd_mutex;
	struct completion *uic_async_done;

	enum ufshcd_state ufshcd_state;
	u32 eh_flags;
	u32 intr_mask;
	u16 ee_ctrl_mask;
	u16 ee_drv_mask;
	u16 ee_usr_mask;
	struct mutex ee_ctrl_mutex;
	bool is_powered;
	bool shutting_down;
	struct semaphore host_sem;

	/* Work Queues */
	struct workqueue_struct *eh_wq;
	struct work_struct eh_work;
	struct work_struct eeh_work;

	/* HBA Errors */
	u32 errors;
	u32 uic_error;
	u32 saved_err;
	u32 saved_uic_err;
	struct ufs_stats ufs_stats;
	bool force_reset;
	bool force_pmc;
	bool silence_err_logs;

	/* Device management request data */
	struct ufs_dev_cmd dev_cmd;
	ktime_t last_dme_cmd_tstamp;

	bool auto_bkops_enabled;

	struct ufs_clk_gating clk_gating;

	struct devfreq *devfreq;
	struct ufs_clk_scaling clk_scaling;

	bool is_sys_suspended;

	enum bkops_status urgent_bkops_lvl;
	bool is_urgent_bkops_lvl_checked;

	struct rw_semaphore clk_scaling_lock;
	unsigned char desc_size[QUERY_DESC_IDN_MAX];
	atomic_t scsi_block_reqs_cnt;

	struct device		bsg_dev;
	struct request_queue	*bsg_queue;
	struct delayed_work rpm_dev_flush_recheck_work;

#ifdef CONFIG_SCSI_UFS_HPB
	struct ufshpb_dev_info hpb_dev;
#endif

	struct ufs_hba_monitor	monitor;

#ifdef CONFIG_SCSI_UFS_CRYPTO
	union ufs_crypto_capabilities crypto_capabilities;
	u32 crypto_cfg_register;
	struct blk_crypto_profile crypto_profile;
#endif
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root;
	struct delayed_work debugfs_ee_work;
	u32 debugfs_ee_rate_limit_ms;
#endif
	u32 luns_avail;
	bool complete_put;
};

static inline bool ufshcd_is_user_access_allowed(struct ufs_hba_priv *priv)
{
	return !priv->shutting_down;
}

void ufshcd_schedule_eh_work(struct ufs_hba *hba);

static inline bool ufshcd_keep_autobkops_enabled_except_suspend(
							struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_KEEP_AUTO_BKOPS_ENABLED_EXCEPT_SUSPEND;
}

static inline u8 ufshcd_wb_get_query_index(struct ufs_hba *hba)
{
	if (hba->dev_info.wb_buffer_type == WB_BUF_MODE_LU_DEDICATED)
		return hba->dev_info.wb_dedicated_lu;
	return 0;
}

#ifdef CONFIG_SCSI_UFS_HWMON
void ufs_hwmon_probe(struct ufs_hba *hba, u8 mask);
void ufs_hwmon_remove(struct ufs_hba *hba);
void ufs_hwmon_notify_event(struct ufs_hba *hba, u8 ee_mask);
#else
static inline void ufs_hwmon_probe(struct ufs_hba *hba, u8 mask) {}
static inline void ufs_hwmon_remove(struct ufs_hba *hba) {}
static inline void ufs_hwmon_notify_event(struct ufs_hba *hba, u8 ee_mask) {}
#endif

int ufshcd_read_desc_param(struct ufs_hba *hba,
			   enum desc_idn desc_id,
			   int desc_index,
			   u8 param_offset,
			   u8 *param_read_buf,
			   u8 param_size);
int ufshcd_query_attr_retry(struct ufs_hba *hba, enum query_opcode opcode,
			    enum attr_idn idn, u8 index, u8 selector,
			    u32 *attr_val);
int ufshcd_query_attr(struct ufs_hba *hba, enum query_opcode opcode,
		      enum attr_idn idn, u8 index, u8 selector, u32 *attr_val);
int ufshcd_query_flag(struct ufs_hba *hba, enum query_opcode opcode,
	enum flag_idn idn, u8 index, bool *flag_res);
void ufshcd_auto_hibern8_update(struct ufs_hba *hba, u32 ahit);

#define SD_ASCII_STD true
#define SD_RAW false
int ufshcd_read_string_desc(struct ufs_hba *hba, u8 desc_index,
			    u8 **buf, bool ascii);

int ufshcd_hold(struct ufs_hba *hba, bool async);
void ufshcd_release(struct ufs_hba *hba);

void ufshcd_map_desc_id_to_length(struct ufs_hba *hba, enum desc_idn desc_id,
				  int *desc_length);

int ufshcd_send_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd);

int ufshcd_exec_raw_upiu_cmd(struct ufs_hba *hba,
			     struct utp_upiu_req *req_upiu,
			     struct utp_upiu_req *rsp_upiu,
			     int msgcode,
			     u8 *desc_buff, int *buff_len,
			     enum query_opcode desc_op);

int ufshcd_wb_toggle(struct ufs_hba *hba, bool enable);

/* Wrapper functions for safely calling variant operations */
static inline const char *ufshcd_get_var_name(struct ufs_hba *hba)
{
	if (hba->vops)
		return hba->vops->name;
	return "";
}

static inline void ufshcd_vops_exit(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->exit)
		return hba->vops->exit(hba);
}

static inline u32 ufshcd_vops_get_ufs_hci_version(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->get_ufs_hci_version)
		return hba->vops->get_ufs_hci_version(hba);

	return ufshcd_readl(hba, REG_UFS_VERSION);
}

static inline int ufshcd_vops_clk_scale_notify(struct ufs_hba *hba,
			bool up, enum ufs_notify_change_status status)
{
	if (hba->vops && hba->vops->clk_scale_notify)
		return hba->vops->clk_scale_notify(hba, up, status);
	return 0;
}

static inline void ufshcd_vops_event_notify(struct ufs_hba *hba,
					    enum ufs_event_type evt,
					    void *data)
{
	if (hba->vops && hba->vops->event_notify)
		hba->vops->event_notify(hba, evt, data);
}

static inline int ufshcd_vops_setup_clocks(struct ufs_hba *hba, bool on,
					enum ufs_notify_change_status status)
{
	if (hba->vops && hba->vops->setup_clocks)
		return hba->vops->setup_clocks(hba, on, status);
	return 0;
}

static inline int ufshcd_vops_hce_enable_notify(struct ufs_hba *hba,
						bool status)
{
	if (hba->vops && hba->vops->hce_enable_notify)
		return hba->vops->hce_enable_notify(hba, status);

	return 0;
}
static inline int ufshcd_vops_link_startup_notify(struct ufs_hba *hba,
						bool status)
{
	if (hba->vops && hba->vops->link_startup_notify)
		return hba->vops->link_startup_notify(hba, status);

	return 0;
}

static inline int ufshcd_vops_pwr_change_notify(struct ufs_hba *hba,
				  enum ufs_notify_change_status status,
				  struct ufs_pa_layer_attr *dev_max_params,
				  struct ufs_pa_layer_attr *dev_req_params)
{
	if (hba->vops && hba->vops->pwr_change_notify)
		return hba->vops->pwr_change_notify(hba, status,
					dev_max_params, dev_req_params);

	return -ENOTSUPP;
}

static inline void ufshcd_vops_setup_task_mgmt(struct ufs_hba *hba,
					int tag, u8 tm_function)
{
	if (hba->vops && hba->vops->setup_task_mgmt)
		return hba->vops->setup_task_mgmt(hba, tag, tm_function);
}

static inline void ufshcd_vops_hibern8_notify(struct ufs_hba *hba,
					enum uic_cmd_dme cmd,
					enum ufs_notify_change_status status)
{
	if (hba->vops && hba->vops->hibern8_notify)
		return hba->vops->hibern8_notify(hba, cmd, status);
}

static inline int ufshcd_vops_apply_dev_quirks(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->apply_dev_quirks)
		return hba->vops->apply_dev_quirks(hba);
	return 0;
}

static inline void ufshcd_vops_fixup_dev_quirks(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->fixup_dev_quirks)
		hba->vops->fixup_dev_quirks(hba);
}

static inline int ufshcd_vops_suspend(struct ufs_hba *hba, enum ufs_pm_op op,
				enum ufs_notify_change_status status)
{
	if (hba->vops && hba->vops->suspend)
		return hba->vops->suspend(hba, op, status);

	return 0;
}

static inline int ufshcd_vops_resume(struct ufs_hba *hba, enum ufs_pm_op op)
{
	if (hba->vops && hba->vops->resume)
		return hba->vops->resume(hba, op);

	return 0;
}

static inline void ufshcd_vops_dbg_register_dump(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->dbg_register_dump)
		hba->vops->dbg_register_dump(hba);
}

static inline int ufshcd_vops_device_reset(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->device_reset)
		return hba->vops->device_reset(hba);

	return -EOPNOTSUPP;
}

static inline void ufshcd_vops_config_scaling_param(struct ufs_hba *hba,
		struct devfreq_dev_profile *p,
		struct devfreq_simple_ondemand_data *data)
{
	if (hba->vops && hba->vops->config_scaling_param)
		hba->vops->config_scaling_param(hba, p, data);
}

extern struct ufs_pm_lvl_states ufs_pm_lvl_states[];

/**
 * ufshcd_scsi_to_upiu_lun - maps scsi LUN to UPIU LUN
 * @scsi_lun: scsi LUN id
 *
 * Returns UPIU LUN id
 */
static inline u8 ufshcd_scsi_to_upiu_lun(unsigned int scsi_lun)
{
	if (scsi_is_wlun(scsi_lun))
		return (scsi_lun & UFS_UPIU_MAX_UNIT_NUM_ID)
			| UFS_UPIU_WLUN_ID;
	else
		return scsi_lun & UFS_UPIU_MAX_UNIT_NUM_ID;
}

int __ufshcd_write_ee_control(struct ufs_hba *hba, u32 ee_ctrl_mask);
int ufshcd_write_ee_control(struct ufs_hba *hba);
int ufshcd_update_ee_control(struct ufs_hba_priv *priv, u16 *mask,
			     u16 *other_mask, u16 set, u16 clr);

static inline int ufshcd_update_ee_drv_mask(struct ufs_hba_priv *priv,
					    u16 set, u16 clr)
{
	return ufshcd_update_ee_control(priv, &priv->ee_drv_mask,
					&priv->ee_usr_mask, set, clr);
}

static inline int ufshcd_update_ee_usr_mask(struct ufs_hba_priv *priv,
					    u16 set, u16 clr)
{
	return ufshcd_update_ee_control(priv, &priv->ee_usr_mask,
					&priv->ee_drv_mask, set, clr);
}

static inline int ufshcd_rpm_get_sync(struct ufs_hba *hba)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);

	return pm_runtime_get_sync(&priv->ufs_device_wlun->sdev_gendev);
}

static inline int ufshcd_rpm_put_sync(struct ufs_hba *hba)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);

	return pm_runtime_put_sync(&priv->ufs_device_wlun->sdev_gendev);
}

static inline void ufshcd_rpm_get_noresume(struct ufs_hba *hba)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);

	pm_runtime_get_noresume(&priv->ufs_device_wlun->sdev_gendev);
}

static inline int ufshcd_rpm_resume(struct ufs_hba *hba)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);

	return pm_runtime_resume(&priv->ufs_device_wlun->sdev_gendev);
}

static inline int ufshcd_rpm_put(struct ufs_hba *hba)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);

	return pm_runtime_put(&priv->ufs_device_wlun->sdev_gendev);
}

/**
 * ufs_is_valid_unit_desc_lun - checks if the given LUN has a unit descriptor
 * @dev_info: pointer of instance of struct ufs_dev_info
 * @lun: LU number to check
 * @return: true if the lun has a matching unit descriptor, false otherwise
 */
static inline bool ufs_is_valid_unit_desc_lun(struct ufs_dev_info *dev_info,
		u8 lun, u8 param_offset)
{
	if (!dev_info || !dev_info->max_lu_supported) {
		pr_err("Max General LU supported by UFS isn't initialized\n");
		return false;
	}
	/* WB is available only for the logical unit from 0 to 7 */
	if (param_offset == UNIT_DESC_PARAM_WB_BUF_ALLOC_UNITS)
		return lun < UFS_UPIU_MAX_WB_LUN_ID;
	return lun == UFS_UPIU_RPMB_WLUN || (lun < dev_info->max_lu_supported);
}

#endif /* _UFSHCD_PRIV_H_ */
