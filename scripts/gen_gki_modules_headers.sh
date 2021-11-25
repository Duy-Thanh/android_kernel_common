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

# generate_header(): $1 = Name of the header file, $2 = input symbol list
function generate_header() {
	local header_file=$1
	local symbol_file=$2

	echo " Generating ${header_file} from ${symbol_file}"
	if [ -f "${header_file}" ]; then
		rm -f -- "${header_file}"
	fi

	if [ ! -s "${symbol_file}" ]; then
		echo " Missing symbol list: ${symbol_file}" >&2
		echo " Failed to generate ${header_file}" >&2
		exit 1
	fi

	# Maximum symbol name length and +1 for the null termination
	local max_name_len=$(awk '{ if ( length > L ) { L=length } }END{ print L }' "${symbol_file}")
	((max_name_len++))

	# Metadata for the header file
	echo "/* SPDX-License-Identifier: GPL-2.0-only */" > "${header_file}"
	echo "/*" >> "${header_file}"
	echo " * DO NOT EDIT" >> "${header_file}"
	echo " *" >> "${header_file}"
	echo " * Build generated header file with GKI module symbols/exports" >> "${header_file}"
	echo " *" >> "${header_file}"
	echo " * Copyright 2021 Google LLC" >> "${header_file}"
	echo " * Author: ramjiyani@google.com (Ramji Jiyani)" >> "${header_file}"
	echo " */" >> "${header_file}"
	echo "" >> "${header_file}"

	if [ "${header_file}" =  "${GKI_PROTECTED_HEADER}" ]; then
		echo "#define NO_OF_PROTECTED_SYMBOLS (sizeof(gki_protected_symbols) / sizeof(gki_protected_symbols[0]))" >> "${header_file}"
		echo "#define MAX_SYM_NAME_LEN (${max_name_len})" >> "${header_file}"
		echo "" >> "${header_file}"
		echo "static const char gki_protected_symbols[][MAX_SYM_NAME_LEN] = {" >> "${header_file}"
	else
		echo "#define NO_OF_EXPORTED_SYMBOLS (sizeof(gki_exported_symbols) / sizeof(gki_exported_symbols[0]))" >> "${header_file}"
		echo "#define MAX_EXPORT_NAME_LEN (${max_name_len})" >> "${header_file}"
		echo "" >> "${header_file}"
		echo "static const char gki_exported_symbols[][MAX_EXPORT_NAME_LEN] = {" >> "${header_file}"
	fi

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

generate_header "${GKI_PROTECTED_HEADER}" "${GKI_PROTECTED_SYMBOLS}"

#
# Generate gki_module_exported.h
#
# Sorted list of exported symbols
GKI_EXPORTED_SYMBOLS="android/abi_gki_modules_exports"

# Header file for exported symbols
GKI_EXPORTED_HEADER="kernel/gki_module_exported.h"

generate_header "${GKI_EXPORTED_HEADER}" "${GKI_EXPORTED_SYMBOLS}"
