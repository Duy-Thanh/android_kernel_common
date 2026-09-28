#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
here=$(dirname "$0")
runner=$here/ppps_run_module.sh
[ -x "$runner" ] || runner=$here/../ppps/ppps_run_module.sh
exec "$runner" "$here/ppps_modules/vmalloc_align_ppps_module.ko" \
	/dev/vmalloc_align_ppps -- "$here/vmalloc_align_ppps" "$@"
