/*
 * SELinux services exported to the rest of the kernel.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2005 Red Hat, Inc., James Morris <jmorris@redhat.com>
 * Copyright (C) 2006 Trusted Computer Solutions, Inc. <dgoeddel@trustedcs.com>
 * Copyright (C) 2006 IBM Corporation, Timothy R. Chavez <tinytim@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/selinux.h>

#include "objsec.h"
#include "security.h"

bool selinux_is_enabled(void)
{
	return selinux_enabled;
}
EXPORT_SYMBOL_GPL(selinux_is_enabled);

void selinux_copy_sid(struct dentry *top, struct dentry *lower)
{
	if (!top || !lower)
		return;
	if (!top->d_inode || !lower->d_inode)
		return;
	if (!top->d_inode->i_security || !lower->d_inode->i_security)
		return;
	top->d_inode->i_security->sid = lower->d_inode->i_security->sid;
}
EXPORT_SYMBOL_GPL(selinux_copy_sid);
