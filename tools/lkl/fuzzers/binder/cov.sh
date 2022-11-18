#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
set -e

script_dir=$(readlink -f $(dirname $0))
repo_root_dir=$(readlink -f ${script_dir}/../../../../../..)
coverage_dir=${repo_root_dir}/out/android12-5.10-lkl/dist/fuzzers/binder_fuzzer/coverage
clang_toolchain_dir=${repo_root_dir}/prebuilts-master/clang/host/linux-x86/clang-r399163b/bin

mkdir -p ${coverage_dir}

${clang_toolchain_dir}/llvm-profdata merge \
  -o ${coverage_dir}/coverage.profdata \
  ${repo_root_dir}/default.profraw

TS=`date +"%m-%d-%Y-%H-%M-%S"`
${clang_toolchain_dir}/llvm-cov show \
  -format=html \
  -output-dir=${coverage_dir}/report_$TS \
  -instr-profile=${coverage_dir}/coverage.profdata \
  -object out/android12-5.10-lkl/dist/fuzzers/binder_fuzzer/binder-fuzzer

