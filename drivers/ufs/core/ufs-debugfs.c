// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Intel Corporation

#include <linux/debugfs.h>

#include "ufs-debugfs.h"
#include <ufs/ufshcd.h>
#include "ufshcd-priv.h"

static struct dentry *ufs_debugfs_root;

struct ufs_debugfs_attr {
	const char			*name;
	mode_t				mode;
	const struct file_operations	*fops;
};

/* @file corresponds to a debugfs attribute in directory hba->debugfs_root. */
static inline struct ufs_hba *hba_from_file(const struct file *file)
{
	return d_inode(file->f_path.dentry->d_parent)->i_private;
}

void __init ufs_debugfs_init(void)
{
	ufs_debugfs_root = debugfs_create_dir("ufshcd", NULL);
}

void ufs_debugfs_exit(void)
{
	debugfs_remove_recursive(ufs_debugfs_root);
}

static int ufs_debugfs_stats_show(struct seq_file *s, void *data)
{
	struct ufs_hba *hba = hba_from_file(s->file);
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);
	struct ufs_event_hist *e = priv->ufs_stats.event;

#define PRT(fmt, typ) \
	seq_printf(s, fmt, e[UFS_EVT_ ## typ].cnt)

	PRT("PHY Adapter Layer errors (except LINERESET): %llu\n", PA_ERR);
	PRT("Data Link Layer errors: %llu\n", DL_ERR);
	PRT("Network Layer errors: %llu\n", NL_ERR);
	PRT("Transport Layer errors: %llu\n", TL_ERR);
	PRT("Generic DME errors: %llu\n", DME_ERR);
	PRT("Auto-hibernate errors: %llu\n", AUTO_HIBERN8_ERR);
	PRT("IS Fatal errors (CEFES, SBFES, HCFES, DFES): %llu\n", FATAL_ERR);
	PRT("DME Link Startup errors: %llu\n", LINK_STARTUP_FAIL);
	PRT("PM Resume errors: %llu\n", RESUME_ERR);
	PRT("PM Suspend errors : %llu\n", SUSPEND_ERR);
	PRT("Logical Unit Resets: %llu\n", DEV_RESET);
	PRT("Host Resets: %llu\n", HOST_RESET);
	PRT("SCSI command aborts: %llu\n", ABORT);
#undef PRT
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ufs_debugfs_stats);

static int ee_usr_mask_get(void *data, u64 *val)
{
	struct ufs_hba *hba = data;
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);

	*val = priv->ee_usr_mask;
	return 0;
}

static int ufs_debugfs_get_user_access(struct ufs_hba *hba)
	__acquires(&priv->host_sem)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);

	down(&priv->host_sem);
	if (!ufshcd_is_user_access_allowed(priv)) {
		up(&priv->host_sem);
		return -EBUSY;
	}
	ufshcd_rpm_get_sync(hba);
	return 0;
}

static void ufs_debugfs_put_user_access(struct ufs_hba *hba)
	__releases(&priv->host_sem)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);

	ufshcd_rpm_put_sync(hba);
	up(&priv->host_sem);
}

static int ee_usr_mask_set(void *data, u64 val)
{
	struct ufs_hba *hba = data;
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);
	int err;

	if (val & ~(u64)MASK_EE_STATUS)
		return -EINVAL;
	err = ufs_debugfs_get_user_access(hba);
	if (err)
		return err;
	err = ufshcd_update_ee_usr_mask(priv, val, MASK_EE_STATUS);
	ufs_debugfs_put_user_access(hba);
	return err;
}

DEFINE_DEBUGFS_ATTRIBUTE(ee_usr_mask_fops, ee_usr_mask_get, ee_usr_mask_set, "%#llx\n");

void ufs_debugfs_exception_event(struct ufs_hba *hba, u16 status)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);
	bool chgd = false;
	u16 ee_ctrl_mask;
	int err = 0;

	if (!priv->debugfs_ee_rate_limit_ms || !status)
		return;

	mutex_lock(&priv->ee_ctrl_mutex);
	ee_ctrl_mask = priv->ee_drv_mask | (priv->ee_usr_mask & ~status);
	chgd = ee_ctrl_mask != priv->ee_ctrl_mask;
	if (chgd) {
		err = __ufshcd_write_ee_control(hba, ee_ctrl_mask);
		if (err)
			dev_err(hba->dev, "%s: failed to write ee control %d\n",
				__func__, err);
	}
	mutex_unlock(&priv->ee_ctrl_mutex);

	if (chgd && !err) {
		unsigned long delay = msecs_to_jiffies(priv->debugfs_ee_rate_limit_ms);

		queue_delayed_work(system_freezable_wq, &priv->debugfs_ee_work, delay);
	}
}

static void ufs_debugfs_restart_ee(struct work_struct *work)
{
	struct ufs_hba_priv *priv = container_of(work, struct ufs_hba_priv,
						 debugfs_ee_work.work);
	struct ufs_hba *hba = &priv->hba;

	if (!priv->ee_usr_mask || pm_runtime_suspended(hba->dev) ||
	    ufs_debugfs_get_user_access(hba))
		return;
	ufshcd_write_ee_control(hba);
	ufs_debugfs_put_user_access(hba);
}

static int ufs_saved_err_show(struct seq_file *s, void *data)
{
	struct ufs_debugfs_attr *attr = s->private;
	struct ufs_hba *hba = hba_from_file(s->file);
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);
	const int *p;

	if (strcmp(attr->name, "saved_err") == 0) {
		p = &priv->saved_err;
	} else if (strcmp(attr->name, "saved_uic_err") == 0) {
		p = &priv->saved_uic_err;
	} else {
		return -ENOENT;
	}

	seq_printf(s, "%d\n", *p);
	return 0;
}

static ssize_t ufs_saved_err_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct ufs_debugfs_attr *attr = file->f_inode->i_private;
	struct ufs_hba *hba = hba_from_file(file);
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);
	char val_str[16] = { };
	int val, ret;

	if (count > sizeof(val_str))
		return -EINVAL;
	if (copy_from_user(val_str, buf, count))
		return -EFAULT;
	ret = kstrtoint(val_str, 0, &val);
	if (ret < 0)
		return ret;

	spin_lock_irq(priv->host->host_lock);
	if (strcmp(attr->name, "saved_err") == 0) {
		priv->saved_err = val;
	} else if (strcmp(attr->name, "saved_uic_err") == 0) {
		priv->saved_uic_err = val;
	} else {
		ret = -ENOENT;
	}
	if (ret == 0)
		ufshcd_schedule_eh_work(hba);
	spin_unlock_irq(priv->host->host_lock);

	return ret < 0 ? ret : count;
}

static int ufs_saved_err_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_saved_err_show, inode->i_private);
}

static const struct file_operations ufs_saved_err_fops = {
	.owner		= THIS_MODULE,
	.open		= ufs_saved_err_open,
	.read		= seq_read,
	.write		= ufs_saved_err_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct ufs_debugfs_attr ufs_attrs[] = {
	{ "stats", 0400, &ufs_debugfs_stats_fops },
	{ "saved_err", 0600, &ufs_saved_err_fops },
	{ "saved_uic_err", 0600, &ufs_saved_err_fops },
	{ }
};

void ufs_debugfs_hba_init(struct ufs_hba *hba)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);
	const struct ufs_debugfs_attr *attr;
	struct dentry *root;

	/* Set default exception event rate limit period to 20ms */
	priv->debugfs_ee_rate_limit_ms = 20;
	INIT_DELAYED_WORK(&priv->debugfs_ee_work, ufs_debugfs_restart_ee);

	root = debugfs_create_dir(dev_name(hba->dev), ufs_debugfs_root);
	if (IS_ERR_OR_NULL(root))
		return;
	priv->debugfs_root = root;
	d_inode(root)->i_private = hba;
	for (attr = ufs_attrs; attr->name; attr++)
		debugfs_create_file(attr->name, attr->mode, root, (void *)attr,
				    attr->fops);
	debugfs_create_file("exception_event_mask", 0600, priv->debugfs_root,
			    hba, &ee_usr_mask_fops);
	debugfs_create_u32("exception_event_rate_limit_ms", 0600, priv->debugfs_root,
			   &priv->debugfs_ee_rate_limit_ms);
}

void ufs_debugfs_hba_exit(struct ufs_hba *hba)
{
	struct ufs_hba_priv *priv = container_of(hba, typeof(*priv), hba);

	debugfs_remove_recursive(priv->debugfs_root);
	cancel_delayed_work_sync(&priv->debugfs_ee_work);
}
