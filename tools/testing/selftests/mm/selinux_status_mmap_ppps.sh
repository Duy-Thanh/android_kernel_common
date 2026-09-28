#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ksft_skip=4
selinuxfs=/sys/fs/selinux
mounted_selinuxfs=false

cleanup()
{
	if $mounted_selinuxfs; then
		umount "$selinuxfs"
	fi
}
trap cleanup EXIT

if [ ! -e "$selinuxfs/status" ]; then
	if [ "$(id -u)" -ne 0 ] || ! command -v mount >/dev/null 2>&1; then
		echo "$0: SELinux status is unavailable"
		exit "$ksft_skip"
	fi
	mkdir -p "$selinuxfs"
	if ! mount -t selinuxfs none "$selinuxfs"; then
		echo "$0: could not mount selinuxfs"
		exit "$ksft_skip"
	fi
	mounted_selinuxfs=true
fi

./selinux_status_mmap_ppps
