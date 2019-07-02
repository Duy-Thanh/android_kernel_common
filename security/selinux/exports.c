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

void selinux_copy_sid(struct dentry *parent, struct dentry *child)
{
	struct inode *pinode, *cinode;
	struct inode_security_struct *pisec, *cisec;

	if (!parent || !child)
		return;
	pinode = parent->d_inode;
	cinode = child->d_inode;
	if (!pinode || !cinode)
		return;
	pisec = pinode->i_security;
	cisec = cinode->i_security;
	if (!pisec || !cisec)
		return;
	pisec->sid = cisec->sid;
}
EXPORT_SYMBOL_GPL(selinux_copy_sid);
