// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} ringbuf SEC(".maps");

/* inputs */
int pid = 0;

/* outputs */
long passed = 0;
long discarded = 0;

SEC("fentry/" SYS_PREFIX "sys_getpgid")
int test_ringbuf_write(void *ctx)
{
	int *foo, cur_pid = bpf_get_current_pid_tgid() >> 32;
	void *sample1, *sample2;

	if (cur_pid != pid)
		return 0;

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
	sample1 = bpf_ringbuf_reserve(&ringbuf, 0x30000, 0);
	if (!sample1)
		return 0;
	/* first one can pass */
	sample2 = bpf_ringbuf_reserve(&ringbuf, 0x30000, 0);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
	sample1 = bpf_ringbuf_reserve(&ringbuf, 0x3000, 0);
	if (!sample1)
		return 0;
	/* first one can pass */
	sample2 = bpf_ringbuf_reserve(&ringbuf, 0x3000, 0);
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
	if (!sample2) {
		bpf_ringbuf_discard(sample1, 0);
		__sync_fetch_and_add(&discarded, 1);
		return 0;
	}
	/* second one must not */
	__sync_fetch_and_add(&passed, 1);
	foo = sample2 + 4084;
	*foo = 256;
	bpf_ringbuf_discard(sample1, 0);
	bpf_ringbuf_discard(sample2, 0);
	return 0;
}
