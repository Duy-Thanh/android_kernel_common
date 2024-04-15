# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the FUSE filesystem.
#

obj-$(CONFIG_FUSE_FS) += fuse.o
obj-$(CONFIG_CUSE) += cuse.o
obj-$(CONFIG_VIRTIO_FS) += virtiofs.o

fuse-y := dev.o dir.o file.o inode.o control.o xattr.o acl.o readdir.o ioctl.o
<<<<<<< HEAD   (43695d Merge 705c1da8fa48 ("Merge tag 'pci-v6.9-changes' of git://g)
fuse-y += passthrough.o
=======
fuse-y += iomode.o
>>>>>>> BRANCH (3c5d12 Merge tag 'mtd/for-6.9' of git://git.kernel.org/pub/scm/linu)
fuse-$(CONFIG_FUSE_DAX) += dax.o
fuse-$(CONFIG_FUSE_PASSTHROUGH) += passthrough.o

virtiofs-y := virtio_fs.o
