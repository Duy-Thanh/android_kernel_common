#! /bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2022, Google LLC. All rights reserved.
# Author: Isaac J. Manjarres <isaacmanjarres@google.com>

progname=${0##*/}

USAGE="USAGE: ${progname} [-s <serialno>] [-o <out_dir>] -l|-m

This script can be used to derive information about the supplier and consumer
relationships between devices on a system, and by extension, their device
drivers.

-s <serialno> specifies a device to connect to when multiple devices
are available, otherwise will default to one available or ANDROID_SERIAL
environment variable.

-o <out_dir> the directory to place the output files in. If this is not
specified, the current directory will be used.

At least one of the following arguments must be provided:

-l causes the script to create a dependency ordered list of drivers found on
the system. Each driver is represented by the compatible string that it used
to match with a device on the system. The list is ordered such that for any
driver entry, that driver does not depend on drivers listed below it, but may
depend on drivers listed above it. This list can be used as is to figure out
what order drivers need to be ported to a new kernel in, as suppliers are
listed first, followed by their consumers. The list can also be reversed and
used to figure out the order in which drivers need to be modularized on an
existing kernel, since that would cause consumers to be listed first, followed
by suppliers.

-m causes the script to create a dependency ordered list of modules found on
the system. Each module is represented by its name. The list is ordered such
that for any module entry, that module does not depend on modules below it, but
may depend on modules listed above it. Since this list contains the supplier
modules first, followed by their consumers, it can be used as the optimal
module loading order."


DIR="$(dirname $(readlink -f $0))"
DEV_NEEDS_HOST_PATH="${DIR}/dev-needs.sh"

DEV_TMP_DIR="/data/local/tmp"
DEV_NEEDS_DEV_PATH="${DEV_TMP_DIR}/$(basename ${DEV_NEEDS_HOST_PATH})"

WANT_DRIVER_DEP_LIST=0
WANT_MOD_LOAD_ORDER=0
OUT_DIR=
DEV_LIST=

prepare_device() {
	adb wait-for-device root >/dev/null
	if [ $? -ne 0 ]; then
		echo "Failed to root device"
		return 1
	fi

	adb push "${DEV_NEEDS_HOST_PATH}" "${DEV_TMP_DIR}/" >/dev/null
	if [ $? -ne 0 ]; then
		echo "Failed to push the dev-needs.sh script to the device"
		return 1
	fi

	return 0
}

cleanup_device() {
	adb shell "rm ${DEV_NEEDS_DEV_PATH}"
}

remove_duplicate_list_entries () {
	local in_file="${1}"

	# Remove duplicates while keeping the list in the same order
	cat -n "${in_file}" | sort -uk2 | sort -n | cut -f2-
}

print_dev_compatible_string() {
	local dev=$1
	local compat_str=$(adb shell "cat '$dev'/of_node/compatible 2>/dev/null" | tr -d '\0')

	if [ -n "${compat_str}" ]; then
		printf "%s\n" "${compat_str}" >> "${2}"
	fi
}

print_dev_driver_compatible_strings() {
	local dev_list="${1}"
	local out_file="${2}"
	local tmp_ordered_driver_list=$(mktemp)
	local deduplicated_list

	for dev in ${dev_list[@]}; do
		print_dev_compatible_string "${dev}" "${tmp_ordered_driver_list}"
	done

	deduplicated_list=$(remove_duplicate_list_entries "${tmp_ordered_driver_list}")
	rm "${tmp_ordered_driver_list}"

	echo "${deduplicated_list}" > "${out_file}"
}

print_dev_module_name() {
	local dev=$1
	local out_file="${2}"
	local module_path=$(adb shell "realpath '$1'/driver/module 2>/dev/null")

	if [ -n "${module_path}" ]; then
		basename "${module_path}" >> "${out_file}"
	fi
}

print_dev_driver_module_names() {
	local dev_list="${1}"
	local out_file="${2}"
	local tmp_ordered_module_list=$(mktemp)
	local deduplicated_list

	for dev in ${dev_list[@]}; do
		print_dev_module_name "${dev}" "${tmp_ordered_module_list}"
	done

	deduplicated_list=$(remove_duplicate_list_entries "${tmp_ordered_module_list}")
	rm "${tmp_ordered_module_list}"

	echo "${deduplicated_list}" > "${out_file}"
}

populate_dev_list() {
	DEV_LIST=$(adb shell 'find /sys/devices -name driver | sed -e "s/\/driver//"')
}

create_device_driver_dependency_list() {
	local driver_out_file="${OUT_DIR}/driver-dependency-list.txt"
	local module_out_file="${OUT_DIR}/module-load-order.txt"
	local tsort_edges
	local ordered_dev_list

	populate_dev_list

	tsort_edges=$(adb shell "${DEV_NEEDS_DEV_PATH} -t '$DEV_LIST'")
	ordered_dev_list=$(echo "${tsort_edges}" | tsort | sed -e "s/\"//g")

	if [ $WANT_DRIVER_DEP_LIST = 1 ]; then
		print_dev_driver_compatible_strings "${ordered_dev_list}" "${driver_out_file}"
	fi

	if [ $WANT_MOD_LOAD_ORDER = 1 ]; then
		print_dev_driver_module_names "${ordered_dev_list}" "${module_out_file}"
	fi
}

while getopts "s:o:lm" f; do
	case "$f" in
	s) export ANDROID_SERIAL="${OPTARG}" ;;
	o) OUT_DIR="${OPTARG}" ;;
	l) WANT_DRIVER_DEP_LIST=1 ;;
	m) WANT_MOD_LOAD_ORDER=1 ;;
	esac
done

if [ $WANT_DRIVER_DEP_LIST = 0 ] && [ $WANT_MOD_LOAD_ORDER = 0 ]; then
	echo "${USAGE}"
	exit 1
elif [ ! -f "${DEV_NEEDS_HOST_PATH}" ]; then
	echo "Kernel does not contain the required dev-needs.sh script"
	exit 1
fi

if [ -z "${OUT_DIR}" ]; then
	OUT_DIR=$(pwd)
elif [ ! -d "${OUT_DIR}" ]; then
	echo "Cannot locate output directory"
	exit 1
fi

if ! prepare_device; then
	exit 1
fi

create_device_driver_dependency_list

cleanup_device
