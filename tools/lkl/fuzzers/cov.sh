#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This is a helper script for collecting code coverage information from a set
# of corpus files. This is designed to test and evaluate fuzzers during fuzzer
# development.

FUZZ_COMPONENT=lkl
FUZZ_TARGET=
while [ $# -gt 0 ]; do
  KEY="$1"
  case $KEY in
    --haiku)
      HAIKU_CORPUS=1
      shift # past value
      ;;
    --x20)
      UPLOAD_TO_X20=1
      shift # past value
      ;;
    *)    # unknown option
      FUZZ_TARGET="$1"
      shift # past argument
      ;;
  esac
done

if [ -z $FUZZ_TARGET ]; then
  echo "Usage: $0 [--haiku] [--x20] <fuzzer name>"
  echo "  --haiku: Use the latest haiku corpus for coverage."
  echo "  --x20:   Uploading the final report to asa-redteam's x20 directory."
  exit 1
fi

# This is a convenient feature to strip the '/' appended to the end of fuzzer
# name when using tab-completion.
FUZZ_TARGET=$(basename $FUZZ_TARGET)

if [ ! -f $FUZZ_TARGET/Build ]; then
  echo "Unknown fuzzer $FUZZ_TARGET"
  exit 1
fi

TOP=../../../../..
FUZZ_BIN=$TOP/out_cov/dist/$FUZZ_TARGET/fuzzers/$FUZZ_TARGET/${FUZZ_TARGET}-fuzzer
FUZZ_DIR=$FUZZ_TARGET/.fuzz

# We need the matching clang in path to do symbolization and coverage support.
CLANG_PATH=$TOP/prebuilts-master/clang/host/linux-x86/clang-r416183c/bin
export PATH=$CLANG_PATH:$PATH

CUR_DATE=$(date +%Y-%m-%d)
CUR_TIME=$(date +"%m-%d-%Y-%H-%M-%S")
CORPUS_DIR=$FUZZ_DIR/corpus
REPORT_DIR=$FUZZ_DIR/coverage/report_$CUR_TIME

if [ "$HAIKU_CORPUS" == "1" ]; then
  HAIKU_CORPUS_ARCHIVE="gs://corpus-backup.internal.clusterfuzz.com/corpus/libFuzzer/android_${FUZZ_TARGET}-fuzzer/latest.zip"
  CORPUS_DIR=/tmp/${FUZZ_TARGET}/corpus
  echo "Downloading corpus files from haiku:$HAIKU_CORPUS_ARCHIVE"
  if ! gsutil cp $HAIKU_CORPUS_ARCHIVE /tmp/${FUZZ_TARGET}_corpus.zip; then
    echo "gsutil failed, did you run 'gcloud auth login' first?"
    exit 1
  fi
  mkdir -p $CORPUS_DIR && rm -rf $CORPUS_DIR/*
  unzip -qouj /tmp/${FUZZ_TARGET}_corpus.zip -d $CORPUS_DIR
  set +e
fi

if ! ls $CORPUS_DIR/* >/dev/null 2>&1; then
    echo "No corpus files found in $CORPUS_DIR, fuzz first?"
    exit 1
fi

mkdir -p $REPORT_DIR
rm -rf $FUZZ_DIR/*.profraw $FUZZ_DIR/default.profdata $FUZZ_DIR/cov.info

# Process the corpus files in a batch of 20, and generate coverage data
export LLVM_PROFILE_FILE=$FUZZ_DIR/%p.profraw
export ASAN_OPTIONS="detect_leaks=0"
find $CORPUS_DIR -type f | xargs -r -n 20 $FUZZ_BIN -close_fd_mask=3

if ! ls $FUZZ_DIR/*.profraw >/dev/null 2>&1; then
    echo "No coverage data generated, make sure you clean build the fuzzer with the follow flag:"
    echo "    BUILD_SOURCE_COVERAGE=TRUE make -C ${FUZZ_TARGET} clean all"
    exit 1
fi

llvm-profdata merge -sparse $FUZZ_DIR/*.profraw -o $FUZZ_DIR/default.profdata
llvm-cov export $FUZZ_BIN -instr-profile=$FUZZ_DIR/default.profdata -format=lcov > $FUZZ_DIR/cov.info
genhtml $FUZZ_DIR/cov.info -o $REPORT_DIR
echo "Coverage report generated in $(realpath $REPORT_DIR)"

if [ "$UPLOAD_TO_X20" == 1 ]; then
  DST_DIR="teams/asa-redteam/coverage/${FUZZ_COMPONENT}/${X20_FOLDER:-haiku}/$CUR_DATE/${FUZZ_TARGET}"
  mkdir -p /google/data/rw/$DST_DIR/
  fileutil cp -f -R -parallelism 100 --disable_progressbar $REPORT_DIR/* /google/data/rw/$DST_DIR/
  echo "Report uploaded to https://x20.corp.google.com/$DST_DIR."
fi

echo "Done."