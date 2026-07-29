#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Regression test for file-backed EROFS images whose filesystem begins at a
# 4K-aligned, but not necessarily native-page-aligned, offset.

KSFT_SKIP=4
OFFSET=4096

echo "TAP version 13"
echo "1..3"

if [ "$(id -u)" -ne 0 ]; then
	echo "ok 1 # SKIP root privileges are required"
	echo "ok 2 # SKIP root privileges are required"
	echo "ok 3 # SKIP root privileges are required"
	exit "$KSFT_SKIP"
fi

if ! command -v mkfs.erofs >/dev/null 2>&1; then
	echo "ok 1 # SKIP mkfs.erofs is not installed"
	echo "ok 2 # SKIP mkfs.erofs is not installed"
	echo "ok 3 # SKIP mkfs.erofs is not installed"
	exit "$KSFT_SKIP"
fi

tmpdir="$(mktemp -d -t erofs-fsoffset-ppps.XXXXXX)"
mounted=
cleanup()
{
	if [ -n "$mounted" ]; then
		umount "$tmpdir/mnt"
	fi
	rm -rf "$tmpdir"
}
trap cleanup EXIT INT TERM

mkdir "$tmpdir/root" "$tmpdir/mnt"
printf '%s\n' "erofs fsoffset ppps" > "$tmpdir/root/payload"

if mkfs.erofs "$tmpdir/image.erofs" "$tmpdir/root" >/dev/null 2>&1; then
	echo "ok 1 create a 4K-block EROFS image"
else
	echo "not ok 1 create a 4K-block EROFS image"
	echo "not ok 2 prepend a 4K archive header"
	echo "not ok 3 mount and read the offset EROFS image"
	exit 1
fi

truncate -s "$OFFSET" "$tmpdir/archive.apex"
if dd if="$tmpdir/image.erofs" of="$tmpdir/archive.apex" bs="$OFFSET" \
	seek=1 conv=notrunc 2>/dev/null; then
	echo "ok 2 prepend a 4K archive header"
else
	echo "not ok 2 prepend a 4K archive header"
	echo "not ok 3 mount and read the offset EROFS image"
	exit 1
fi

if mount -t erofs -o "fsoffset=$OFFSET" \
	"$tmpdir/archive.apex" "$tmpdir/mnt"; then
	mounted=1
	if cmp "$tmpdir/root/payload" "$tmpdir/mnt/payload"; then
		echo "ok 3 mount and read the offset EROFS image"
		exit 0
	fi
fi

echo "not ok 3 mount and read the offset EROFS image"
exit 1
