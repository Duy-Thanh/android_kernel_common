#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Load a PPPS fixture module, make sure its device node exists, run a test
# command against it and unload the module again.
#
#   ppps_run_module.sh [-d <dependency module>]... <ko> <devnode> [<sysfs-dev>]
#                      -- <command> [<args>...]
#
# <ko>         module file, relative to this script's directory or to the
#              current directory (a bare "<name>.ko" next to the tests is
#              also tried, for flat installs).
# <devnode>    device node the test opens, e.g. /dev/gup_retry_ppps.  If it
#              does not appear by itself it is created from <sysfs-dev>.
# <sysfs-dev>  sysfs "dev" file (may be a glob) holding major:minor;
#              defaults to /sys/class/misc/<basename of devnode>/dev.  When a
#              candidate has a sibling "name" file it must match the devnode
#              basename (UIO devices).
# -d <module>  a module to load first (./<module>.ko or modprobe) and unload
#              last, e.g. ttm.
#
# Exit codes follow kselftest: 4 (skip) when not root or the module is not
# available, 1 when loading fails, otherwise the command's exit status.

ksft_skip=4
here=$(dirname "$0")
deps=
loaded=
created_device=
loaded_deps=

usage()
{
	echo "usage: $0 [-d module]... <ko> <devnode> [sysfs-dev] -- command..." >&2
	exit 1
}

while [ "$1" = "-d" ]; do
	deps="$deps $2"
	shift 2
done
[ $# -ge 3 ] || usage
ko=$1
device=$2
shift 2
sysfs_dev=
if [ "$1" != "--" ]; then
	sysfs_dev=$1
	shift
fi
[ "$1" = "--" ] || usage
shift
[ $# -ge 1 ] || usage

module_name=$(basename "$ko" .ko)
[ -z "$sysfs_dev" ] && sysfs_dev="/sys/class/misc/$(basename "$device")/dev"

cleanup()
{
	status=$?
	trap - EXIT
	if [ -n "$created_device" ]; then
		rm -f "$device"
	fi
	if [ -n "$loaded" ]; then
		if ! rmmod "$module_name"; then
			echo "$0: failed to unload $module_name" >&2
			status=1
		fi
	fi
	for dep in $loaded_deps; do
		if ! rmmod "$dep"; then
			echo "$0: failed to unload $dep" >&2
			status=1
		fi
	done
	exit "$status"
}

if [ "$(id -u)" -ne 0 ]; then
	echo "$0: must be run as root"
	exit "$ksft_skip"
fi
if ! command -v insmod >/dev/null 2>&1; then
	echo "$0: insmod is unavailable"
	exit "$ksft_skip"
fi

for candidate in "$here/$ko" "$ko" "$here/$(basename "$ko")" \
		 "./$(basename "$ko")"; do
	if [ -f "$candidate" ]; then
		ko=$candidate
		break
	fi
done
if [ ! -f "$ko" ]; then
	echo "$0: test module $module_name is unavailable"
	exit "$ksft_skip"
fi

trap cleanup EXIT

for dep in $deps; do
	# Do not remove a dependency that was loaded before this invocation.
	[ -d "/sys/module/$(echo "$dep" | tr - _)" ] && continue
	if [ -f "./$dep.ko" ]; then
		insmod "./$dep.ko" || exit 1
	elif command -v modprobe >/dev/null 2>&1; then
		if ! modprobe "$dep"; then
			echo "$0: $dep module is unavailable"
			exit "$ksft_skip"
		fi
	else
		echo "$0: $dep module is unavailable"
		exit "$ksft_skip"
	fi
	loaded_deps="$dep $loaded_deps"
done

if insmod "$ko"; then
	loaded=1
elif [ ! -e "$device" ]; then
	echo "$0: failed to load $ko"
	exit 1
fi

if [ ! -e "$device" ]; then
	name=$(basename "$device")
	dev=
	for candidate in $sysfs_dev; do
		[ -f "$candidate" ] || continue
		if [ -f "$(dirname "$candidate")/name" ] &&
		   [ "$(cat "$(dirname "$candidate")/name")" != "$name" ]; then
			continue
		fi
		dev=$(cat "$candidate")
		break
	done
	if [ -z "$dev" ]; then
		echo "$0: $name was not registered"
		exit 1
	fi
	# The device manager (ueventd/udev) may create the node concurrently.
	if mknod "$device" c "${dev%:*}" "${dev#*:}"; then
		created_device=1
	elif [ ! -e "$device" ]; then
		exit 1
	fi
	chmod 0600 "$device"
fi

"$@"
