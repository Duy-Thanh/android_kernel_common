/* vim:set ts=8 sw=8 tw=0 noet ft=c:
 *
 * fs/sdcardfs/inode.c
 *
 * Copyright (C) 2017 HUAWEI, Inc.
 * Author: gaoxiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include "sdcardfs.h"
#include <linux/fs_struct.h>
#include <linux/version.h>

#include "trace-events.h"

static struct dentry *touch_file(struct dentry *parent, char *name, int len)
{
	struct dentry *dentry;

	inode_lock(d_inode(parent));
	dentry = lookup_one_len(name, parent, len);

	if (unlikely(dentry == NULL))
		dentry = ERR_PTR(-ENOENT);
	else if (!IS_ERR(dentry)) {
		int err = (d_is_positive(dentry) ?
			 -EEXIST : vfs_create(d_inode(parent),
			 dentry, S_IFREG | 0664, 0));
		if (err) {
			dput(dentry);
			dentry = ERR_PTR(err);
		}
	}

	inode_unlock(d_inode(parent));
	return dentry;
}

static int touch_nomedia(struct dentry *parent)
{
	struct dentry *d_nomedia =
		touch_file(parent, ".nomedia", sizeof(".nomedia") - 1);

	if (IS_ERR(d_nomedia))
		return PTR_ERR(d_nomedia);

	dput(d_nomedia);
	return 0;
}

/* When creating /Android/data and /Android/obb, mark them as .nomedia */
static int prepare_nomedia_dir(struct sdcardfs_sb_info *sbi,
	struct dentry *parent,
	struct dentry *real_dentry
) {
	int err = 0;
	struct sdcardfs_tree_entry *pte = SDCARDFS_D(parent);
	const char *name = real_dentry->d_name.name;

	if (unlikely(pte->perm == PERM_ANDROID)) {
		if (unlikely(!strcasecmp(name, "data"))) {
touch_real:
			err = touch_nomedia(real_dentry);
			WARN_ON(err == -EEXIST);
		} else if (unlikely(!strcasecmp(name, "obb"))) {
			/* not multiuser obb */
			if (sbi->shared_obb == NULL)
				goto touch_real;

			err = touch_nomedia(sbi->shared_obb);
			if (err == -EEXIST)
				err = 0;
		}

		if (unlikely(err))
			errln("failed to touch .nomedia in %s: %d", name, err);
	}
	return err;
}

int sdcardfs_create_file(struct inode *dir,
	struct dentry *dentry,
	umode_t mode,
	bool want_excl,
	const char *__caller_FUNCTION__)
{
	const char *name = dentry->d_name.name;
	struct dentry *parent, *real_dir_dentry;
	struct sdcardfs_sb_info *sbi;
	const struct cred *saved_cred;
	int err;
	struct dentry *real_dentry;
	struct fs_struct *saved_fs;

	if (d_really_is_positive(dentry)) {
		warnln("%s, unexpected positive dentry(%s)",
			__caller_FUNCTION__, name);
		return -ESTALE;
	}

	/* some forbidden filenames should be checked before creating */
	if (permission_denied_to_create(dir, name)) {
		errln("permission denied to create %s", name);
		return -EACCES;
	}

	parent = dget_parent(dentry);
	BUG_ON(d_inode(parent) != dir);

	real_dir_dentry = sdcardfs_get_lower_dentry(parent);
	BUG_ON(real_dir_dentry == NULL);

	inode_lock_nested(d_inode(real_dir_dentry), I_MUTEX_PARENT);

	sbi = SDCARDFS_SB(dir->i_sb);
	/* save current_cred and override it */
	OVERRIDE_CRED(sbi, saved_cred);
	if (IS_ERR(saved_cred)) {
		err = PTR_ERR(saved_cred);
		goto unlock_err;
	}

#ifdef SDCARDFS_CASE_INSENSITIVE
	if (sbi->ci->may_create != NULL) {
		struct path path = {
			.dentry = real_dir_dentry,
			.mnt = sbi->lower_mnt
		};

		err = sbi->ci->may_create(&path, &dentry->d_name);
		if (err)
			goto revert_cred_err;
	}
#endif

	real_dentry = lookup_one_len(dentry->d_name.name,
		real_dir_dentry, dentry->d_name.len);

	if (IS_ERR(real_dentry)) {
		err = PTR_ERR(real_dentry);
		goto revert_cred_err;
	}

	if (d_is_positive(real_dentry)) {
		err = -ESTALE;
		goto dput_err;
	}

	BUG_ON(sbi->override_fs == NULL);
	saved_fs = override_current_fs(sbi->override_fs);

	switch (mode & S_IFMT) {
	case S_IFDIR:
		/* for directories, the last 16bit of mode is 0775 */
		mode = S_IFDIR | 0775;

		err = vfs_mkdir(d_inode(real_dir_dentry),
			real_dentry, mode);

		if (!err && d_is_positive(real_dentry))
			err = prepare_nomedia_dir(sbi, parent, real_dentry);
		break;

	case S_IFREG:
		/* for regular files, the last 16bit of mode is 0664 */
		mode = S_IFREG | 0664;

		err = vfs_create(d_inode(real_dir_dentry),
			real_dentry, mode, want_excl);
		break;

	default:
		err = -EINVAL;
		break;
	}

	fsstack_copy_inode_size(dir, d_inode(real_dir_dentry));
	revert_current_fs(saved_fs);

	if (err)
		goto dput_err;

	REVERT_CRED(saved_cred);

	err = PTR_ERR(sdcardfs_interpose(parent, dentry, real_dentry));
	if (unlikely(err))
		errln("%s, unexpected error when interposing: %d",
			__caller_FUNCTION__, err);

	goto unlock_err;
dput_err:
	dput(real_dentry);
revert_cred_err:
	REVERT_CRED(saved_cred);
unlock_err:
	inode_unlock(d_inode(real_dir_dentry));
	dput(real_dir_dentry);
	dput(parent);
	return err;
}

static int sdcardfs_create(struct inode *dir,
	struct dentry *dentry,
	umode_t __maybe_unused mode, bool want_excl)
{
	int err;

	trace_sdcardfs_create_enter(dir, dentry, mode, want_excl);

	err = sdcardfs_create_file(dir, dentry,
		S_IFREG, want_excl, __func__);

	trace_sdcardfs_create_exit(dir, dentry, mode, want_excl, err);
	return err;
}

static int sdcardfs_mkdir(struct inode *dir,
	struct dentry *dentry, umode_t __maybe_unused mode)
{
	int err;

	trace_sdcardfs_mkdir_enter(dir, dentry, mode);

#ifdef CONFIG_SDCARD_FS_RESERVED_SPACE
	if (!check_min_free_space(dir->i_sb, 0, 1)) {
		errln("%s, No minimum free space.", __func__);
		err = -ENOSPC;
		goto out;
	}
#endif
	err = sdcardfs_create_file(dir, dentry, S_IFDIR, false, __func__);

out:
	trace_sdcardfs_mkdir_exit(dir, dentry, mode, err);
	return err;
}

static int sdcardfs_remove_file(struct inode *dir,
	struct dentry *dentry)
{
	struct dentry *real_dentry;
	struct dentry *real_dir_dentry;
	struct inode *real_dir;
	int err;
	const struct cred *saved_cred;
	struct inode *real_inode = NULL;

	/* some forbidden filenames should be checked before removing */
	if (permission_denied_to_remove(dir, dentry->d_name.name)) {
		errln("permission denied to remove %s", dentry->d_name.name);
		return -EACCES;
	}

	real_dentry = sdcardfs_get_real_dentry(dentry);
	BUG_ON(real_dentry == NULL);

retry:
	real_dir_dentry = dget_parent(real_dentry);

	/* TODO: disconnected dentry is not supported yet.*/
	BUG_ON(real_dir_dentry == NULL);

	/*
	 * note that real_dir_dentry ?(!=) lower_dentry(dget_parent(dentry)).
	 * it's unsafe to check by use IS_ROOT since inode_lock has not taken
	 */
	if (unlikely(real_dentry == real_dir_dentry)) {
		err = -EBUSY;
		goto dput_err;
	}

	real_dir = d_inode(real_dir_dentry);
	inode_lock_nested(real_dir, I_MUTEX_PARENT);

	if (unlikely(real_dir_dentry != real_dentry->d_parent)) {
		inode_unlock(real_dir);
		dput(real_dir_dentry);
		goto retry;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(dir->i_sb), saved_cred);
	if (IS_ERR(saved_cred)) {
		err = PTR_ERR(saved_cred);
		goto unlock_err;
	}

	/* real_dentry must be hashed and in the real_dir */
	if (__read_seqcount_retry(&real_dentry->d_seq,
		SDCARDFS_D(dentry)->real.d_seq)) {
		err = -ESTALE;

		/* since we dont support hashed but negative dentry */
		d_invalidate(dentry);
		goto revert_cred_err;
	}

	real_inode = d_inode(real_dentry);
	if (S_ISDIR(real_inode->i_mode)) {
		err = vfs_rmdir(real_dir, real_dentry);
		real_inode = NULL;
	} else {
		ihold(real_inode);
		err = vfs_unlink(real_dir, real_dentry, NULL);
	}
	fsstack_copy_inode_size(dir, real_dir);
revert_cred_err:
	REVERT_CRED(saved_cred);
unlock_err:
	inode_unlock(d_inode(real_dir_dentry));
dput_err:
	dput(real_dir_dentry);
	dput(real_dentry);

	/*
	 * Since sdcardfs is a multiview stackable file system,
	 * we must keep track of the underlay file system all the time.
	 * Thus, it makes no sense that hashed but negative
	 * dentries exist (probably could cause bad behaviors).
	 */
	if (!err)
		d_drop(dentry);
	if (real_inode != NULL)
		iput(real_inode);
	return err;
}

static int sdcardfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int ret;

	trace_sdcardfs_unlink_enter(dir, dentry);
	ret = sdcardfs_remove_file(dir, dentry);
	trace_sdcardfs_unlink_exit(dir, dentry, ret);
	return ret;
}

static int sdcardfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int ret;

	trace_sdcardfs_rmdir_enter(dir, dentry);
	ret = sdcardfs_remove_file(dir, dentry);
	trace_sdcardfs_rmdir_exit(dir, dentry, ret);
	return ret;
}

#ifdef SDCARDFS_CASE_INSENSITIVE
static inline struct dentry *rename_lookup_ci(
	struct sdcardfs_sb_info *sbi,
	struct dentry *dir, struct qstr *name)
{
	struct dentry *ret = NULL;

	if (sbi->ci->lookup != NULL) {
		struct path path = {.dentry = dir, .mnt = sbi->lower_mnt};

		ret = sbi->ci->lookup(&path, name, true);
		/* once again sbi->ci->lookup() never returns NULL */
		if (IS_ERR(ret)) {
			if (ret == ERR_PTR(-ENOENT))
				ret = NULL;
		/*
		 * hashed (see d_delete) or
		 * unhashed(by d_alloc) but negative
		 */
		} else if (d_is_negative(ret)) {
			dput(ret);
			ret = ERR_PTR(-ESTALE);
		}
	}
	return ret;
}
#endif

static int sdcardfs_rename(struct inode *old_dir, struct dentry *old_dentry,
	struct inode *new_dir, struct dentry *new_dentry)
{
	int err;
	struct dentry *trap, *dentry;
	struct dentry *real_old_parent, *real_new_parent;
	struct dentry *real_old_dentry, *real_new_dentry;
	const struct cred *saved_cred;
	bool overlapped = true;

	trace_sdcardfs_rename_enter(old_dir, old_dentry, new_dir, new_dentry);

	/* some forbidden filenames should be checked before removing */
	if (permission_denied_to_remove(old_dir, old_dentry->d_name.name)
		|| permission_denied_to_create(new_dir, new_dentry->d_name.name)
		|| permission_denied_to_remove(new_dir, new_dentry->d_name.name)) {
		err = -EACCES;
		goto out;
	}

	/*
	 * since old_dir, new_old both have inode_locked, so
	 * it is no need to use dget_parent
	 */
	real_old_dentry = sdcardfs_get_real_dentry(old_dentry);
	real_old_parent = dget_parent(real_old_dentry);

	/*
	 * note that real_dir_dentry ?(!=) lower_dentry(dget_parent(dentry)).
	 * and it's unsafe to check by use IS_ROOT since inode_lock isnt taken
	 */
	if (unlikely(real_old_parent == real_old_dentry)) {
		err = -EBUSY;
		goto dput_err;
	}

	real_new_parent = sdcardfs_get_lower_dentry(new_dentry->d_parent);

	trap = lock_rename(real_old_parent, real_new_parent);

	/* source should not be ancestor of target */
	if (real_old_dentry == trap) {
		err = -EINVAL;
		goto unlock_err;
	}

	err = -ESTALE;
	/* avoid race between dget_parent and lock_rename */
	if (unlikely(real_old_parent != real_old_dentry->d_parent))
		goto unlock_err;

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(old_dir->i_sb), saved_cred);
	if (IS_ERR(saved_cred)) {
		err = PTR_ERR(saved_cred);
		goto unlock_err;
	}

	/* real_old_dentry must be hashed and in the real_old_dir */
	dentry = lookup_one_len(real_old_dentry->d_name.name,
		real_old_parent, real_old_dentry->d_name.len);
	if (IS_ERR(dentry)) {
		/* maybe some err or real_old_parent DEADDIR */
		err = PTR_ERR(dentry);
		goto revert_cred_err;
	}

	dput(dentry);

	if (real_old_dentry != dentry)
		goto revert_cred_err;

	/* real_target may be a negative unhashed dentry */
#ifdef SDCARDFS_CASE_INSENSITIVE
	dentry = rename_lookup_ci(SDCARDFS_SB(new_dentry->d_sb),
		real_new_parent, &new_dentry->d_name);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto revert_cred_err;
	} else if (dentry != NULL && dentry != real_old_dentry) {
		real_new_dentry = dentry;
		/* target should not be ancestor of source */
		if (dentry == trap) {
			err = -ENOTEMPTY;
			goto dput2_err;
		}
	} else {
		if (dentry == real_old_dentry)
			dput(dentry);
		/*
		 * and if dentry == real_old_dentry, new_dentry
		 * could be positive
		 */
#endif
		real_new_dentry = lookup_one_len(new_dentry->d_name.name,
			real_new_parent, new_dentry->d_name.len);
		if (IS_ERR(real_new_dentry)) {
			err = PTR_ERR(real_new_dentry);
			goto revert_cred_err;
		}
		overlapped = d_is_positive(real_new_dentry);
#ifdef SDCARDFS_CASE_INSENSITIVE
	}
#endif

	err = vfs_rename(d_inode(real_old_parent), real_old_dentry,
		d_inode(real_new_parent), real_new_dentry, NULL, 0);

	dentry = new_dentry->d_parent;
	get_derived_permission4(dentry,
		old_dentry, new_dentry->d_name.name, true);

	fsstack_copy_inode_size(old_dir, d_inode(real_new_parent));
	if (new_dir != old_dir)
		fsstack_copy_inode_size(new_dir, d_inode(real_new_parent));

dput2_err:
	dput(real_new_dentry);
revert_cred_err:
	REVERT_CRED(saved_cred);
unlock_err:
	unlock_rename(real_old_parent, real_new_parent);
	dput(real_new_parent);
dput_err:
	dput(real_old_parent);
	dput(real_old_dentry);

out:
	trace_sdcardfs_rename_exit(old_dir, old_dentry, new_dir, new_dentry, err);
	return err;
}

static int sdcardfs_setattr(struct dentry *dentry, struct iattr *ia)
{
	int err;
	struct iattr copied_ia;
	struct inode *inode = d_inode(dentry);

	/*
	 * since sdcardfs uses its own uid/gid derived policy,
	 * so uid/gid modification is unsupported
	 */
	if (unlikely(ia->ia_valid & ATTR_FORCE)) {
		copied_ia = *ia;
		copied_ia.ia_valid &= ~(ATTR_UID | ATTR_GID | ATTR_MODE);
		ia = &copied_ia;
	} else
	/* We strictly follow the fat/exfat file system behavior */
	if (((ia->ia_valid & ATTR_UID) &&
		!uid_eq(ia->ia_uid, inode->i_uid)) ||
		((ia->ia_valid & ATTR_GID) &&
		!gid_eq(ia->ia_gid, inode->i_gid)) ||
		((ia->ia_valid & ATTR_MODE) &&
		(ia->ia_mode & ~SDCARDFS_VALID_MODE))) {
		err = SDCARDFS_SB(dentry->d_sb)->options.quiet ? 0 : -EPERM;
		goto out;
	} else
	/*
	 * We don't return -EPERM here. Yes, strange, but this is too
	 * old behavior.
	 */
	if (ia->ia_valid & ATTR_MODE)
		ia->ia_valid &= ~ATTR_MODE;

	err = inode_change_ok(inode, ia);
	if (!err) {
		struct dentry *lower_dentry;

		if (ia->ia_valid & ATTR_SIZE) {
			err = inode_newsize_ok(inode, ia->ia_size);
			if (err)
				goto out;
			truncate_setsize(inode, ia->ia_size);
		}

		if (ia->ia_valid & ATTR_FILE) {
			struct file *lower_file = sdcardfs_lower_file(ia->ia_file);

			WARN_ON(lower_file == NULL);
			ia->ia_file = lower_file;
		}

		lower_dentry = sdcardfs_get_lower_dentry(dentry);

		if (lower_dentry != NULL) {
			const struct cred *saved_cred;

			/* Allow touch updating timestamps. */
			ia->ia_valid |= ATTR_FORCE;

			/* save current_cred and override it */
			OVERRIDE_CRED(SDCARDFS_SB(dentry->d_sb), saved_cred);
			if (unlikely(IS_ERR(saved_cred)))
				err = PTR_ERR(saved_cred);
			else {
				inode_lock(d_inode(lower_dentry));
				err = notify_change(lower_dentry, ia, NULL);
				inode_unlock(d_inode(lower_dentry));

				REVERT_CRED(saved_cred);
			}
			dput(lower_dentry);
		}
	}
out:
	return err;
}

static int sdcardfs_getattr(struct vfsmount *mnt, struct dentry *dentry,
	struct kstat *stat)
{
	int err;
	struct path lower_path;
	const struct cred *saved_cred;

	debugln("%s, dentry=%p, name=%s", __func__,
		dentry, dentry->d_name.name);

	if (sdcardfs_get_lower_path(dentry, &lower_path)) {
		WARN_ON(1);
		err = -ESTALE;
		goto out;
	}

	/* save current_cred and override it */
	OVERRIDE_CRED(SDCARDFS_SB(mnt->mnt_sb), saved_cred);
	if (IS_ERR(saved_cred)) {
		err = PTR_ERR(saved_cred);
		goto out_pathput;
	}

	err = vfs_getattr(&lower_path, stat);
	REVERT_CRED(saved_cred);

	if (!err) {
		struct inode *inode = d_inode(dentry);
		struct sdcardfs_tree_entry *te = SDCARDFS_I(inode);

		/* note that generic_fillattr dont take any lock */

		if (te->revision > inode->i_version) {
			inode_lock(inode);
			__fix_derived_permission(te, inode);
			inode_unlock(inode);
		}
		stat->uid = inode->i_uid;
		stat->gid = inode->i_gid;
		stat->mode = inode->i_mode;
		stat->dev = inode->i_sb->s_dev;	/* fix df statistic */
	}

out_pathput:
	_path_put(&lower_path);
out:
	return err;
}

static int sdcardfs_permission(struct inode *inode, int mask)
{
	bool need_reval;
#ifdef CONFIG_SDCARD_FS_PLUGIN_PRIVACY_SPACE
	struct sdcardfs_sb_info *sbi;
#endif
	struct sdcardfs_tree_entry *te = SDCARDFS_I(inode);

	need_reval = te->revision > inode->i_version;

	if (need_reval) {
		if (mask & MAY_NOT_BLOCK)
			return -ECHILD;

		__fix_derived_permission(te, inode);
	}

	/* have no access to PERM_JAILHOUSE via sdcardfs */
	if (unlikely(te->perm == PERM_JAILHOUSE))
		return -EACCES;

#ifdef CONFIG_SDCARD_FS_PLUGIN_PRIVACY_SPACE
	sbi = SDCARDFS_SB(inode->i_sb);

	if (unlikely(sbi->blocked_userid >= 0)) {
		uid_t uid = from_kuid(&init_user_ns, current_fsuid());

		if (multiuser_get_user_id(uid) == sbi->blocked_userid &&
			multiuser_get_app_id(uid) != sbi->appid_excluded)
			return -EACCES;
	}
#endif

	return generic_permission(inode, mask);
}

const struct inode_operations sdcardfs_dir_iops = {
	.create     = sdcardfs_create,
	.lookup     = sdcardfs_lookup,
	.permission = sdcardfs_permission,
	.unlink     = sdcardfs_unlink,
	.mkdir      = sdcardfs_mkdir,
	.rmdir      = sdcardfs_rmdir,
	.rename     = sdcardfs_rename,
	.setattr    = sdcardfs_setattr,
	.getattr    = sdcardfs_getattr,

#ifdef CONFIG_SDCARD_FS_XATTR
	.setxattr = sdcardfs_setxattr,
	.getxattr = sdcardfs_getxattr,
	.listxattr = sdcardfs_listxattr,
	.removexattr = sdcardfs_removexattr,
#endif
};

const struct inode_operations sdcardfs_main_iops = {
	.permission = sdcardfs_permission,
	.setattr    = sdcardfs_setattr,
	.getattr    = sdcardfs_getattr,

#ifdef CONFIG_SDCARD_FS_XATTR
	.setxattr = sdcardfs_setxattr,
	.getxattr = sdcardfs_getxattr,
	.listxattr = sdcardfs_listxattr,
	.removexattr = sdcardfs_removexattr,
#endif
};
