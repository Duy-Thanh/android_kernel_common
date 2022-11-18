#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e
set -x

ROOT_DIR=$(readlink -m $(dirname $0)/../../../..)
cd $ROOT_DIR
if [ ! -f build/build.sh ];then
    echo "Not a valid kernel repo."
    exit 1
fi

if [ -z "$FUZZERS" ]; then
    FUZZERS="${FUZZERS} hid"
    FUZZERS="${FUZZERS} vfat"
    FUZZERS="${FUZZERS} rndis"
    FUZZERS="${FUZZERS} binder"
    FUZZERS="${FUZZERS} virtio_blk"
    FUZZERS="${FUZZERS} virtio_ring"
    FUZZERS="${FUZZERS} virtio_pci"
fi

if [ ! -z "${ENABLE_SOURCE_COVERAGE}" ]; then
    LKL_LINE_COV=1
    OUT_DIR_SUFFIX=_cov
fi

OUT_DIR=${OUT_DIR:-${ROOT_DIR}/out${OUT_DIR_SUFFIX}}
DIST_DIR="${DIST_DIR:-${OUT_DIR}/dist}"

mkdir -p ${OUT_DIR} ${DIST_DIR}

OUT_DIR=$(realpath ${OUT_DIR})
DIST_DIR=$(realpath ${DIST_DIR})

KERN_DIR="${KERN_DIR:-common}"
ACK_CONFIG_DIR="${KERN_DIR}/tools/lkl/ack_configs"
ZIP_LIST=""
for fuzzer in $FUZZERS; do
    if [ ! -f ${ACK_CONFIG_DIR}/build.config.lkl_kasan.${fuzzer}-fuzzer ];then
        echo "Build config for ${fuzzer}-fuzzer doesn't exist."
        exit 1
    fi

    OUT_DIR=${OUT_DIR}/${fuzzer} \
        DIST_DIR=${DIST_DIR}/${fuzzer} \
        LKL_LINE_COV=${LKL_LINE_COV} \
        BUILD_CONFIG=${ACK_CONFIG_DIR}/build.config.lkl_kasan.${fuzzer}-fuzzer \
        build/build.sh "$@"

    ZIP_LIST="${ZIP_LIST} ${DIST_DIR}/${fuzzer}/fuzzers/${fuzzer}/${fuzzer}-fuzzer.zip"
done

zip -jq ${DIST_DIR}/fuzz-host-x86_64.zip ${ZIP_LIST}
