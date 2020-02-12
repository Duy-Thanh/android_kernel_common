#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# Create an autoksyms.h header file from the list of all module's needed symbols
# as recorded on the second line of *.mod files and the user-provided symbol
# whitelist.

set -e

output_file="$1"

# Use "make V=1" to debug this script.
case "$KBUILD_VERBOSE" in
*1*)
	set -x
	;;
esac

# We need access to CONFIG_ symbols
. include/config/auto.conf

# Use 'eval' to expand the whitelist path and check if it is relative
eval ksym_wl="${CONFIG_UNUSED_KSYMS_WHITELIST:-/dev/null}"
[ "${ksym_wl:0:1}" = "/" ] || ksym_wl="$abs_srctree/$ksym_wl"

# Generate a new ksym list file with symbols needed by the current
# set of modules.
cat > "$output_file" << EOT
/*
 * Automatically generated file; DO NOT EDIT.
 */

EOT

[ -f modules.order ] && modlist=modules.order || modlist=/dev/null
sed 's/ko$/mod/' $modlist |
xargs -n1 sed -n -e '2{s/ /\n/g;/^$/!p;}' -- |
cat - "$ksym_wl" |
sort -u |
sed -e 's/\(.*\)/#define __KSYM_\1 1/' >> "$output_file"

# Special case for modversions (see modpost.c)
if [ -n "$CONFIG_MODVERSIONS" ]; then
	echo "#define __KSYM_module_layout 1" >> "$output_file"
fi
