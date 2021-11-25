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

# We need access to CONFIG_ symbols
. ${OUT_DIR}/include/config/auto.conf

# Exit early with success if module protection is not enabled
if [ "${CONFIG_MODULE_SIG_PROTECT}" != "y" ]; then
	echo " GKI Module Symbol Protection Disabled"
	exit 0
fi

#
# Common Definitions
#

# Sorted List of Protected Symbols
GKI_PROTECTED_SYMBOLS="android/abi_gki_aarch64_modules_protected"

# File to generate
GKI_PROTECTED_HEADER="kernel/gki_module_protected.h"

if [ ! -f ${GKI_PROTECTED_SYMBOLS} ] || [ ! -s ${GKI_PROTECTED_SYMBOLS} ]; then
	echo " Missing Symbol list: ${GKI_PROTECTED_SYMBOLS}" >&2
	echo " Failed to generate ${GKI_PROTECTED_HEADER}" >&2
	exit 1
fi

# Sorted List of Protected Symbols
GKI_EXPORTED_SYMBOLS="android/abi_gki_aarch64_modules_exports"

# File to generate
GKI_EXPORTED_HEADER="kernel/gki_module_exported.h"

if [ ! -f ${GKI_EXPORTED_SYMBOLS} ] || [ ! -s ${GKI_EXPORTED_SYMBOLS} ]; then
	echo " Missing Symbol list: ${GKI_EXPORTED_SYMBOLS}" >&2
	echo " Failed to generate ${GKI_EXPORTED_HEADER}" >&2
	exit 1
fi

#
# Generate gki_module_protected.h
#

echo " Genearating ${GKI_PROTECTED_HEADER}"
if [ -f ${GKI_PROTECTED_HEADER} ]; then
	rm -f ${GKI_PROTECTED_HEADER}
fi

# No of lines(symbols) in file and -1 for the header (1st) line
NO_OF_SYMBOLS=$(wc -l < "$GKI_PROTECTED_SYMBOLS")
((NO_OF_SYMBOLS--))

# Maximum symbol name length and +1 for the null termination
MAX_SYM_NAME_LEN=$(awk '{ if ( length > L ) { L=length } }END{ print L }' "$GKI_PROTECTED_SYMBOLS")
((MAX_SYM_NAME_LEN++))

cat > "$GKI_PROTECTED_HEADER" << EOT
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * DO NOT EDIT
 *
 * Build Generated Header File with GKI Protected Symbols
 *
 * Copyright 2021 Google LLC
 * Author: ramjiyani@google.com (Ramji Jiyani)
 */

#define NO_OF_PROTECTED_SYMBOLS (${NO_OF_SYMBOLS})
#define MAX_SYM_NAME_LEN       (${MAX_SYM_NAME_LEN})

static const char gki_protected_symbols[NO_OF_PROTECTED_SYMBOLS][MAX_SYM_NAME_LEN] = {
EOT

# Loop through the file and add symbols in an array except the 1st line
sed 1d $GKI_PROTECTED_SYMBOLS | while read sym
do
   echo "	\"${sym}\"," >> ${GKI_PROTECTED_HEADER}
done

# Terminate the file
echo "};" >> $GKI_PROTECTED_HEADER

#
# Generate gki_module_exported.h
#

echo " Genearating ${GKI_EXPORTED_HEADER}"

if [ -f ${GKI_EXPORTED_HEADER} ]; then
	rm -f ${GKI_EXPORTED_HEADER}
fi

# No of lines(exports) in file and -1 for the header (1st) line
NO_OF_EXPORTS=$(wc -l < "$GKI_EXPORTED_SYMBOLS")
((NO_OF_EXPORTS--))

# Maximum symbol name length and +1 for the null termination
MAX_EXPORT_NAME_LEN=$(awk '{ if ( length > L ) { L=length } }END{ print L }' "$GKI_EXPORTED_SYMBOLS")
((MAX_EXPORT_NAME_LEN++))

cat > "$GKI_EXPORTED_HEADER" << EOT
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * DO NOT EDIT
 *
 * Build Generated Header File with GKI Exported Symbols
 *
 * Copyright 2021 Google LLC
 * Author: ramjiyani@google.com (Ramji Jiyani)
 */

#define NO_OF_EXPORTED_SYMBOLS (${NO_OF_EXPORTS})
#define MAX_EXPORT_NAME_LEN    (${MAX_EXPORT_NAME_LEN})

static const char gki_exported_symbols[NO_OF_EXPORTED_SYMBOLS][MAX_EXPORT_NAME_LEN] = {
EOT

# Loop through the file and add symbols in an array except the 1st line
sed 1d $GKI_EXPORTED_SYMBOLS | while read sym
do
   echo "	\"${sym}\"," >> ${GKI_EXPORTED_HEADER}
done

# Terminate the file
echo "};" >> $GKI_EXPORTED_HEADER
