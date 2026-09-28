#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

cached_dir=/tmp/v9fs-mmap-close-ppps-cached.$$
cached_mounted=

cleanup()
{
	if [ -n "$cached_mounted" ]; then
		umount "$cached_dir"
	fi
	rmdir "$cached_dir"
}

if [ "$(id -u)" -ne 0 ]; then
	echo "SKIP: root privileges are required"
	exit 4
fi

mkdir -p "$cached_dir"
trap cleanup EXIT

if ! mount -t 9p -o trans=virtio,version=9p2000.L,cache=loose,msize=262144 \
	ppps9pc "$cached_dir"; then
	echo "SKIP: 9p mount tag ppps9pc is required"
	exit 4
fi
cached_mounted=1

./v9fs_mmap_close_ppps "$cached_dir"
