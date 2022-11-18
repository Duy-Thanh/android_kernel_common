#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# This is a helper script for running fuzzer continuously and classifying
# crashes by the stacktraces. It's designed to test and evaluate fuzzers during
# development.
FUZZ_TARGET=
while [ $# -gt 0 ]; do
  KEY="$1"
  case $KEY in
    --haiku)
      HAIKU_CORPUS=1
      shift # past value
      ;;
    *)    # unknown option
      FUZZ_TARGET="$1"
      shift # past argument
      ;;
  esac
done

if [ -z $FUZZ_TARGET ]; then
  echo "Usage: $0 [--haiku] <fuzzer name>"
  echo "  --haiku:   Download the latest corpus files from haiku and merge into local corpus directory."
  exit 1
fi

# This is a convient feature to strip the '/' appended to the end of fuzzer
# name when using tab-completion.
FUZZ_TARGET=$(basename $FUZZ_TARGET)

if [ ! -f $FUZZ_TARGET/Build ]; then
  echo "Unknown fuzzer $FUZZ_TARGET"
  exit 1
fi

TOP=../../../../..
FUZZ_BIN=$TOP/out/dist/$FUZZ_TARGET/fuzzers/$FUZZ_TARGET/${FUZZ_TARGET}-fuzzer
FUZZ_DIR=$FUZZ_TARGET/.fuzz

# We need the matching clang in path to do symbolization and coverage support.
CLANG_PATH=$TOP/prebuilts-master/clang/host/linux-x86/clang-r416183c/bin
export PATH=$CLANG_PATH:$PATH

if [ ! -d $FUZZ_DIR/corpus ]; then
    mkdir -p $FUZZ_DIR/corpus
    cp $FUZZ_TARGET/seeds/* $FUZZ_DIR/corpus/
    unzip -q $FUZZ_DIR/corpus.zip -d $FUZZ_DIR/corpus/
fi

if [ "$HAIKU_CORPUS" == "1" ]; then
  HAIKU_CORPUS_ARCHIVE="gs://corpus-backup.internal.clusterfuzz.com/corpus/libFuzzer/android_${FUZZ_TARGET}-fuzzer/latest.zip"
  CORPUS_DIR=$FUZZ_DIR/corpus
  echo "Downloading corpus files from haiku:$HAIKU_CORPUS_ARCHIVE"
  if ! gsutil cp $HAIKU_CORPUS_ARCHIVE /tmp/${FUZZ_TARGET}_corpus.zip; then
    echo "gsutil failed, did you run 'gcloud auth login' first?"
    exit 1
  fi

  mkdir -p $CORPUS_DIR
  unzip -qouj /tmp/${FUZZ_TARGET}_corpus.zip -d $CORPUS_DIR
fi

trap ctrl_c INT
function ctrl_c() {
  echo "Packing up corpus..."
  zip -rjq $FUZZ_DIR/corpus.zip $FUZZ_DIR/corpus
  exit
}

CMN_OPTIONS+=" -timeout=3"
CMN_OPTION+=" -close_fd_mask=3"
CMN_OPTIONS+=" -max_len=512"
CMN_OPTIONS+=" -quiet=1"

FUZZ_OPTIONS="$CMN_OPTIONS"
FUZZ_OPTIONS+=" -max_total_time=6000"

MERGE_OPTIONS="$CMN_OPTIONS"
VERIFY_OPTIONS="$CMN_OPTIONS"

function reduce_corpus() {
  echo "Consolidating corpus..."
  rm -rf $FUZZ_DIR/corpus.old
  mv $FUZZ_DIR/corpus $FUZZ_DIR/corpus.old
  mkdir $FUZZ_DIR/corpus
  ${FUZZ_BIN} ${MERGE_OPTIONS} -merge=1 $FUZZ_DIR/corpus $FUZZ_DIR/corpus.old
  rm $FUZZ_DIR/corpus.zip
  zip -rqj $FUZZ_DIR/corpus.zip $FUZZ_DIR/corpus
}

function verify_crash() {
  local CRASH_BIN=${1}
  echo "Trying ${FUZZ_BIN} ${CRASH_BIN} ..."
  if ${FUZZ_BIN} ${VERIFY_OPTIONS} ${CRASH_BIN} > $FUZZ_DIR/crash.log 2>&1; then
    echo "Not reproducing, continue..."
    rm ${CRASH_BIN} $FUZZ_DIR/crash.log
    return
  fi

  grep -E "#[[:digit:]]+\s0x[0-9a-f]+ in" $FUZZ_DIR/crash.log | sed -E 's/0x[0-9a-f]+//g' | sed -n '0,/LLVMFuzzerTestOneInput/p' > $FUZZ_DIR/trace.log
  SIGNATURE=CRASH-$(md5sum $FUZZ_DIR/trace.log | cut -f1 -d ' ')
  if [ ! -d $FUZZ_DIR/issues/${SIGNATURE} ];then
    cat $FUZZ_DIR/crash.log
    echo "New issue category found: $FUZZ_DIR/issues/${SIGNATURE}"
    mkdir -p $FUZZ_DIR/issues/${SIGNATURE}
  fi
  mv ${CRASH_BIN} $FUZZ_DIR/issues/${SIGNATURE}
  mv $FUZZ_DIR/crash.log $FUZZ_DIR/issues/${SIGNATURE}/$(basename ${CRASH_BIN}).log
  mv $FUZZ_DIR/trace.log $FUZZ_DIR/issues/${SIGNATURE}/$(basename ${CRASH_BIN})_trace.log
}

function run_job() {
  LOGFILE=$FUZZ_DIR/logs/crash_$(date +"%m-%d-%Y-%H-%M-%S").log
  ${FUZZ_BIN} ${FUZZ_OPTIONS} -artifact_prefix=$FUZZ_DIR/ $FUZZ_DIR/corpus > ${LOGFILE} 2>&1
  for CRASH_BIN in $(ls $FUZZ_DIR/timeout-* $FUZZ_DIR/crash-* $FUZZ_DIR/oom-* 2>/dev/null);
  do
    verify_crash ${CRASH_BIN}
  done

  cp ${LOGFILE} fuzz-sum.log
  grep "cov: " fuzz-*.log | sort -n -k 4 | tail -n 5
  rm fuzz-*.log

  echo "'${FUZZ_BIN} ${FUZZ_OPTIONS} $FUZZ_DIR/corpus' finished."

  if ls $FUZZ_DIR/leak-* >/dev/null 2>&1; then
    echo "There seem to be memory leaks. If leak detection is not needed, run this script with the following environment varialbe:"
    echo "   ASAN_OPTIONS='detect_leaks=0'"
    rm $FUZZ_DIR/leak-*
  fi
}

mkdir -p $FUZZ_DIR/corpus.old $FUZZ_DIR/issues $FUZZ_DIR/logs
LAST_CORPUS_COUNT=0
while true
do
  CORPUS_COUNT=$(ls -1 $FUZZ_DIR/corpus | wc -l)
  DIFF=$(( ${CORPUS_COUNT} - ${LAST_CORPUS_COUNT} ))
  echo "last=${LAST_CORPUS_COUNT}, count=${CORPUS_COUNT}, diff=${DIFF}"
  if [ ${DIFF} -gt 1000 ];then
    reduce_corpus
    LAST_CORPUS_COUNT=$(ls -1 $FUZZ_DIR/corpus | wc -l)
  fi
  run_job
done
