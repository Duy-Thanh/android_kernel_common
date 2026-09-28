#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Run mmu_notifier_ppps inside a fresh memory cgroup with its fixture module
# loaded.  Needs MGLRU's debugfs interface, cgroup v2 and swap.

ksft_skip=4
here=$(dirname "$0")
runner=$here/ppps_run_module.sh
[ -x "$runner" ] || runner=$here/../ppps/ppps_run_module.sh
cgroup=/sys/fs/cgroup/ppps-mmu-notifier

if [ ! -e /sys/kernel/debug/lru_gen ] || [ ! -e /sys/fs/cgroup/cgroup.procs ]; then
	echo "$0: MGLRU debugfs or cgroup v2 is unavailable"
	exit "$ksft_skip"
fi
if [ "$(awk 'NR > 1 { count++ } END { print count + 0 }' /proc/swaps)" -eq 0 ]; then
	echo "$0: swap is required to drive anonymous-memory reclaim"
	exit "$ksft_skip"
fi
if [ "$(id -u)" -ne 0 ]; then
	echo "$0: must be run as root"
	exit "$ksft_skip"
fi

echo +memory > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
mkdir -p "$cgroup"
trap 'rmdir "$cgroup" >/dev/null 2>&1 || true' EXIT

"$runner" "$here/ppps_modules/mmu_notifier_ppps_module.ko" \
	/dev/mmu_notifier_ppps -- \
	sh -c 'echo $$ > "$1/cgroup.procs" && exec "$2"' sh "$cgroup" \
	"$here/mmu_notifier_ppps"
