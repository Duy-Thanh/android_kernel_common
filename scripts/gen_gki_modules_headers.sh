#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright 2021 Google LLC
# Author: ramjiyani@google.com (Ramji Jiyani)
#

#
# Generates hearder files for GKI modules symbol and export protections
#
# gki_module_exported.h -- Syms protected from _export_ by unsigned modules
# gki_module_protected.h -- Syms protected from _access_ by unsigned modules
#

set -e

# We need access to CONFIG_ symbols
. "${OUT_DIR}/include/config/auto.conf"

# Exit early with success if module protection is not enabled
if [ "${CONFIG_MODULE_SIG_PROTECT}" != "y" ]; then
	echo " GKI Module Symbol Protection Disabled" >&2
	exit 0
fi

#
# Common Definitions
#

#
# generate_header():
# Args: $1 = Name of the header file
#       $2 = Input symbol list
#       $3 = Symbol type (protected/exported)
#
function generate_header() {
	local header_file=$1
	local symbol_file=$2
	local symbol_type=$3

	echo " Generating ${header_file} from ${symbol_file}"
	if [ -f "${header_file}" ]; then
		rm -f -- "${header_file}"
	fi

	if [ ! -s "${symbol_file}" ]; then
		echo " Missing ${symbol_type} symbol list: ${symbol_file}" >&2
		echo " Failed to generate ${header_file}" >&2
		exit 1
	fi

	# Find Maximum symbol name length
	# Skip 1st line (symbol header), Trim white spaces and +1 for null termination
	local max_name_len=$(awk '
			{
				$1=$1;
				if ( length > L && NR > 1) {
					L=length
				}
			} END { print ++L }' "${symbol_file}")

	# Metadata for the header file
	cat > "${header_file}" <<- EOT
	/* SPDX-License-Identifier: GPL-2.0-only */
	/*
	 * DO NOT EDIT
	 *
	 * Build generated header file with GKI module symbols/exports
	 *
	 * Copyright 2021 Google LLC
	 * Author: ramjiyani@google.com (Ramji Jiyani)
	 */

	EOT

	# Macro definitions
	echo "#define NO_OF_${symbol_type^^}_SYMBOLS \\"	>> "${header_file}"
	echo -e "\t(sizeof(gki_${symbol_type}_symbols) / sizeof(gki_${symbol_type}_symbols[0]))"\
								>> "${header_file}"
	echo "#define MAX_${symbol_type^^}_NAME_LEN (${max_name_len})"\
								>> "${header_file}"
	echo "" >> "${header_file}"

	# Symbol array
	echo "static const char gki_${symbol_type}_symbols[][MAX_${symbol_type^^}_NAME_LEN] = {"\
								>> "${header_file}"

	# Loop through the file and add symbols in an array except the 1st line
	sed 1d "${symbol_file}" | while read sym
	do
	   echo "	\"${sym}\"," >> "${header_file}"
	done

	# Terminate the file
	echo "};" >> "${header_file}"
}

#
# Generate gki_module_protected.h
#
# Sorted list of protected symbols
GKI_PROTECTED_SYMBOLS="android/abi_gki_modules_protected"

# Header file for protected symbols
GKI_PROTECTED_HEADER="kernel/gki_module_protected.h"

generate_header "${GKI_PROTECTED_HEADER}" "${GKI_PROTECTED_SYMBOLS}" "protected"

#
# Generate gki_module_exported.h
#
# Sorted list of exported symbols
GKI_EXPORTED_SYMBOLS="android/abi_gki_modules_exports"

# Header file for exported symbols
GKI_EXPORTED_HEADER="kernel/gki_module_exported.h"

generate_header "${GKI_EXPORTED_HEADER}" "${GKI_EXPORTED_SYMBOLS}" "exported"
