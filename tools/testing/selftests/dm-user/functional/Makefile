# SPDX-License-Identifier: GPL-2.0
INCLUDES := -I../include -I../../ -Iusr/include
CFLAGS := $(CFLAGS) -g -O2 -Wall -static -D_GNU_SOURCE $(INCLUDES) -pthread

HEADERS := \
	../include/logging.h
TEST_GEN_FILES := \
	simple-read-all \
	short-read-all

TEST_PROGS := run.sh

top_srcdir = ../../../../..
KSFT_KHDR_INSTALL := 1
include ../../lib.mk

$(TEST_GEN_FILES): $(HEADERS)
