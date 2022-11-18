#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
set -e

fuzzer_name=vfat-fuzzer

trap ctrl_c INT

function ctrl_c() {
  echo "Packing up corpus..."
  zip -q -r -j ${fuzzer_name}_seed_corpus.zip .fuzz/corpus
  exit
}

FUZZ_OPTIONS="$FUZZ_OPTIONS -quiet=1"
FUZZ_OPTIONS="$FUZZ_OPTIONS -timeout=10"
FUZZ_OPTIONS="$FUZZ_OPTIONS -close_fd_mask=3"

mkdir -p .fuzz/result .fuzz/corpus .fuzz/corpus.old || true
cp seeds/* .fuzz/corpus

unzip -q -o ${fuzzer_name}_seed_corpus.zip -d .fuzz/corpus || true

LAST_CORPUS_COUNT=$(ls -1 .fuzz/corpus | wc -l)

function reduce_corpus() {
    echo "Consolidating corpus..."
    rm -rf .fuzz/corpus.old/*
    mv .fuzz/corpus/* .fuzz/corpus.old/
    ./${fuzzer_name} $FUZZ_OPTIONS -merge=1 .fuzz/corpus .fuzz/corpus.old
    rm ${fuzzer_name}_seed_corpus.zip
    zip -q -r -j ${fuzzer_name}_seed_corpus.zip .fuzz/corpus
}

while true
do
  CORPUS_COUNT=$(ls -1 .fuzz/corpus | wc -l)
  DIFF=$(( $CORPUS_COUNT - $LAST_CORPUS_COUNT ))
  echo "last=$LAST_CORPUS_COUNT, count=$CORPUS_COUNT, diff=$DIFF"
  if [ $DIFF -gt 100 ];then
    reduce_corpus
    LAST_CORPUS_COUNT=$(ls -1 .fuzz/corpus | wc -l)
  fi

  ./${fuzzer_name} $FUZZ_OPTIONS .fuzz/corpus 2>&1 | tee ./crash.log || true

  CRASH_ID=$(grep "Test unit written to ./crash-" crash.log | grep -o "crash-.*$") || true
  if [ -z $CRASH_ID ]; then
    echo "No crash found, continue..."
    continue
  fi

  if ./${fuzzer_name}./$CRASH_ID > ./crash.log 2>&1; then
    echo "Not crashing, continue..."
    rm ./$CRASH_ID
    continue
  fi

  gdb -batch \
    -ex "set print address off" \
    -ex "set print frame-arguments none" \
    -ex "set print frame-info short-location" \
    -ex "run ./$CRASH_ID" \
    -ex "bt" \
    ./${fuzzer_name} > ./gdb.log 2>&1

  SIGNATURE=CRASH-$(grep -E '^#[[:digit:]]+ ' ./gdb.log | md5sum | cut -f1 -d ' ')

  if [ ! -d ./issues/$SIGNATURE ];then
    cat ./gdb.log
    echo "New issue category found: ./issues/$SIGNATURE"
    cp -r ./issues/template ./issues/$SIGNATURE
    cp ./$CRASH_ID ./issues/$SIGNATURE/input
    cp ./crash.log ./issues/$SIGNATURE/crash.txt
    cp ./gdb.log   ./issues/$SIGNATURE/gdb.txt
  fi

  echo "Crash moved to: .fuzz/result/$SIGNATURE/$CRASH_ID.bin"
  mkdir -p .fuzz/result/$SIGNATURE
  mv ./$CRASH_ID .fuzz/result/$SIGNATURE/$CRASH_ID.bin
  mv ./crash.log .fuzz/result/$SIGNATURE/$CRASH_ID.log
  mv ./gdb.log .fuzz/result/$SIGNATURE/${CRASH_ID}_gdb.log

  echo "'./${fuzzer_name} $FUZZ_OPTIONS .fuzz/corpus' finished."
done
