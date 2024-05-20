# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the FUSE filesystem.
#

obj-$(CONFIG_FUSE_FS) += fuse.o
obj-$(CONFIG_CUSE) += cuse.o
obj-$(CONFIG_VIRTIO_FS) += virtiofs.o

fuse-y := dev.o dir.o file.o inode.o control.o xattr.o acl.o readdir.o ioctl.o
<<<<<<< HEAD   (b82710 Merge 68bf6bfdcf56 ("Merge tag 'ext4_for_linus-6.9-rc1' of g)
fuse-y += passthrough.o
=======
fuse-y += iomode.o
>>>>>>> BRANCH (6ce8b2 Merge tag 'fuse-update-6.9' of git://git.kernel.org/pub/scm/)
fuse-$(CONFIG_FUSE_DAX) += dax.o
fuse-$(CONFIG_FUSE_PASSTHROUGH) += passthrough.o

virtiofs-y := virtio_fs.o
