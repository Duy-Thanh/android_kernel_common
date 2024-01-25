#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

BAZEL=tools/bazel
BIN_DIR=common/tools/testing/android/bin
ACLOUD=$BIN_DIR/acloudb.sh
TRADEFED=prebuilts/tradefed/filegroups/tradefed/tradefed.sh
TESTSDIR=bazel-bin/common/

print_help() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "This script builds kernel, launches cvd and runs selftests on it."
    echo "Available options:"
    echo "  --skip-kernel-build    Skip the kernel building step"
    echo "  --skip-cvd-launch      Skip the CVD launch step"
    echo "  --skip-cvd-kill        Do not kill CVD launched by running this script"
    echo "  -d, --dist-dir=DIR     The kernel dist dir (default is /tmp/kernel_dist)"
    echo "  -s, --serial=SERIAL    The device serial number with -s. If specified, cuttlefish device launch will be skipped"
    echo "  -h, --help             Display this help message and exit"
    echo ""
    exit 0
}

BUILD_KERNEL=true
LAUNCH_CVD=true
KILL_CVD=true
DIST_DIR=/tmp/kernel_dist
SERIAL_NUMBER=

while test $# -gt 0; do
    case "$1" in
        -h|--help)
            print_help
            ;;
        --skip-kernel-build)
            BUILD_KERNEL=false
            shift
            ;;
        --skip-cvd-launch)
            LAUNCH_CVD=false
            shift
            ;;
        --skip-cvd-kill)
            KILL_CVD=false
            shift
            ;;
        -d)
            shift
            if test $# -gt 0; then
                DIST_DIR=$1
            else
                echo "kernel distribution directory is not specified"
                exit 1
            fi
            shift
            ;;
        --dist-dir*)
            DIST_DIR=`echo $1 | sed -e 's/^[^=]*=//g'`
            shift
            ;;
        -s)
            shift
            if test $# -gt 0; then
                SERIAL_NUMBER=$1
                BUILD_KERNEL=false
                LAUNCH_CVD=false
                KILL_CVD=false
            else
                echo "device serial is not specified"
                exit 1
            fi
            shift
            ;;
        --serial*)
            BUILD_KERNEL=false
            LAUNCH_CVD=false
            KILL_CVD=false
            SERIAL_NUMBER=`echo $1 | sed -e 's/^[^=]*=//g'`
            shift
            ;;
        *)
            ;;
    esac
done

if $BUILD_KERNEL; then
    echo "Building kernel..."
    $BAZEL run //common-modules/virtual-device:virtual_device_x86_64_dist --  --dist_dir=$DIST_DIR
fi

if $LAUNCH_CVD; then
    echo "Launching cvd..."
    CVD_OUT=$($ACLOUD create --local-kernel-image $DIST_DIR)
    echo $CVD_OUT
    INSTANCE_NAME=$(echo "$CVD_OUT" | grep -o "ins-[^\[]*")
    SERIAL_NUMBER=$(echo "$CVD_OUT" | grep -oE 'device serial: ([0-9]+\.){3}[0-9]+:[0-9]+' | sed 's/device serial: //')
    echo "acloud launched device $SERIAL_NUMBER with instance $INSTANCE_NAME"
fi

if [ -z "$SERIAL_NUMBER" ]; then
    echo "Device serial number is not provided by acloud or by -s|--serial flag"
    exit 1
else
    echo "Test with device: $SERIAL_NUMBER"
fi

echo "Get abi from device $SERIAL_NUMBER"
ABI=$(adb -s $SERIAL_NUMBER shell getprop ro.product.cpu.abi)
echo "Building kselftests according to device $SERIAL_NUMBER ro.product.cpu.abi $ABI ..."
case $ABI in
	arm64*)
		$BAZEL build //common:kselftest_tests_arm64
		;;
	x86_64*)
		$BAZEL build //common:kselftest_tests_x86_64
		;;
	*)
		echo "$ABI not supported"
		exit 1
		;;
esac

$TRADEFED run commandAndExit template/local_min --template:map test=suite/test_mapping_suite \
--include-filter selftests --tests-dir=$TESTSDIR -s $SERIAL_NUMBER

if $LAUNCH_CVD && $KILL_CVD; then
    echo "Test finished. Deleting cvd..."
    $ACLOUD delete --instance-names $INSTANCE_NAME
fi
