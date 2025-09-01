# SPDX-License-Identifier: GPL-2.0

top_srcdir = ../../../../..

CFLAGS += -Wall -Wl,--no-as-needed -O2 -g -I$(top_srcdir)/usr/include $(KHDR_INCLUDES)

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
TEST_PROGS := mptcp_connect.sh mptcp_connect_mmap.sh mptcp_connect_sendfile.sh \
	      mptcp_connect_checksum.sh pm_netlink.sh mptcp_join.sh diag.sh \
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
TEST_PROGS := mptcp_connect.sh pm_netlink.sh mptcp_join.sh diag.sh \
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
	      simult_flows.sh mptcp_sockopt.sh userspace_pm.sh

TEST_GEN_FILES = mptcp_connect pm_nl_ctl mptcp_sockopt mptcp_inq

TEST_FILES := mptcp_lib.sh settings

TEST_INCLUDES := ../lib.sh ../net_helper.sh

EXTRA_CLEAN := *.pcap

include ../../lib.mk
