# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the FUSE filesystem.
#

obj-$(CONFIG_FUSE_FS) += fuse.o
obj-$(CONFIG_CUSE) += cuse.o
obj-$(CONFIG_VIRTIO_FS) += virtiofs.o

fuse-y := dev.o dir.o file.o inode.o control.o xattr.o acl.o readdir.o ioctl.o
<<<<<<< HEAD   (a365b2 Merge ca7e91776912 ("Merge tag 'x86-apic-2024-03-10' of git:)
fuse-y += passthrough.o
=======
fuse-y += iomode.o
>>>>>>> BRANCH (5e74df Merge tag 'x86-urgent-2024-03-24' of git://git.kernel.org/pu)
fuse-$(CONFIG_FUSE_DAX) += dax.o
fuse-$(CONFIG_FUSE_PASSTHROUGH) += passthrough.o

virtiofs-y := virtio_fs.o
