#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# A depmod wrapper

if test $# -ne 1 -a $# -ne 2; then
	echo "Usage: $0 <kernelrelease> [System.map folder]" >&2
	exit 1
fi

KERNELRELEASE=$1
KBUILD_MIXED_TREE=$2

: ${DEPMOD:=depmod}

<<<<<<< HEAD   (1e42b2 Revert "ANDROID: kbuild: Add support for installing out-of-t)
if ! test -r ${KBUILD_MIXED_TREE}System.map ; then
||||||| BASE
if ! test -r System.map ; then
=======
if ! test -r "${objtree}/System.map" ; then
>>>>>>> BRANCH (6a34df Merge tag 'kbuild-v6.13' of git://git.kernel.org/pub/scm/lin)
	echo "Warning: modules_install: missing 'System.map' file. Skipping depmod." >&2
	exit 0
fi

# legacy behavior: "depmod" in /sbin, no /sbin in PATH
PATH="$PATH:/sbin"
if [ -z $(command -v $DEPMOD) ]; then
	echo "Warning: 'make modules_install' requires $DEPMOD. Please install it." >&2
	echo "This is probably in the kmod package." >&2
	exit 0
fi

<<<<<<< HEAD   (1e42b2 Revert "ANDROID: kbuild: Add support for installing out-of-t)
set -- -ae -F ${KBUILD_MIXED_TREE}System.map
||||||| BASE
set -- -ae -F System.map
=======
set -- -ae -F "${objtree}/System.map"
>>>>>>> BRANCH (6a34df Merge tag 'kbuild-v6.13' of git://git.kernel.org/pub/scm/lin)
if test -n "$INSTALL_MOD_PATH"; then
	set -- "$@" -b "$INSTALL_MOD_PATH"
fi
exec "$DEPMOD" "$@" "$KERNELRELEASE"
