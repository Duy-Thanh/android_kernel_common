#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
here=$(dirname "$0")
runner=$here/ppps_run_module.sh
[ -x "$runner" ] || runner=$here/../ppps/ppps_run_module.sh
exec "$runner" "$here/ppps_modules/sg_page_slices_ppps_module.ko" \
	/dev/sg_page_slices_ppps -- "$here/sg_page_slices_ppps" "$@"
