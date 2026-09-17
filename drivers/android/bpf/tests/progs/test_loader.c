// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Google LLC
 *
 * Minimal BPF program to validate BPF FD loading via light skeleton.
 */

#define __uint(name, val) int (*name)[val]
#define __type(name, val) typeof(val) *name
#define SEC(name) __attribute__((section(name), used))

static void *(*bpf_map_lookup_elem)(void *map, const void *key) = (void *) 1;

struct {
	__uint(type, 2); /* BPF_MAP_TYPE_ARRAY */
	__uint(max_entries, 4);
	__type(key, unsigned int);
	__type(value, unsigned long long);
} test_map SEC(".maps");

SEC("socket")
int probe(void *ctx)
{
	unsigned int key = 0;
	unsigned long long *val = bpf_map_lookup_elem(&test_map, &key);

	return val ? (int)*val : 0;
}

char _license[] SEC("license") = "GPL";
