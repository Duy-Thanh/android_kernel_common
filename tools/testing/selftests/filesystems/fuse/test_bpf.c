// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google

#define __EXPORTED_HEADERS__

#include <uapi/linux/types.h>
#include <uapi/linux/bpf_fuse.h>
#include <uapi/linux/fuse.h>

#define SEC(NAME) __attribute__((section(NAME), used))

static long (*bpf_trace_printk)(const char *fmt, __u32 fmt_size, ...)
	= (void *) 6;

#define bpf_printk(fmt, ...)					\
	({			                                \
		char ____fmt[] = fmt;                           \
	        bpf_trace_printk(____fmt, sizeof(____fmt),      \
		                 ##__VA_ARGS__);                \
	})

SEC("dummy")

#define fake_name "fake"

inline int strcmp(const char *a, const char *b)
{
	int i;

	for (i = 0; i < __builtin_strlen(b) + 1; ++i)
		if (a[i] != b[i])
			return -1;

	return 0;
}

SEC("test_trace")

/* return 1 to use backing fs, 0 to pass to usermode */
int trace(struct bpf_fuse_data *ctx)
{
	switch (ctx->fuse_opcode) {
	case FUSE_LOOKUP: {
		/* real and partial use backing file */
		int backing = strcmp(ctx->name, "real") == 0 ||
			strcmp(ctx->name, "partial") == 0;

		bpf_printk("Paul: lookup %s %d", ctx->name, backing);
		return backing ? 1 : 0;
	}

	case FUSE_GETATTR: {
		/* real and partial use backing file */
		int backing = strcmp(ctx->name, "/") == 0 ||
			strcmp(ctx->name, "real") == 0 ||
			strcmp(ctx->name, "partial") == 0;

		bpf_printk("Paul: getattr %s %d", ctx->name, backing);
		return backing ? 1 : 0;
	}

	case FUSE_OPEN: {
		int backing = 0;

		if (strcmp(ctx->name, "real") == 0)
			backing = 1;

		else if (strcmp(ctx->name, "partial") == 0)
			backing = 2;

		bpf_printk("Paul: open %s %d", ctx->name, backing);
		return backing;
	}

	case FUSE_READ: {
		bpf_printk("Paul: read %llu %llu",
			   ctx->file_handle, ctx->offset);
		if (ctx->file_handle == 1 && ctx->offset == 0)
			return 0;
		return 1;
	}

	case FUSE_OPENDIR: {
		int backing = 0;

		if (strcmp(ctx->name, "/") == 0)
			backing = 1;

		bpf_printk("Paul: opendir %s %d", ctx->name, backing);
		return backing;
	}

	case FUSE_READDIR: {
		int backing = 0;

		if (strcmp(ctx->name, "/") == 0)
			backing = FUSE_BPF_USER_FILTER | FUSE_BPF_BACKING |
				  FUSE_BPF_POST_FILTER;

		bpf_printk("Paul: readdir %s %d", ctx->name, backing);
		return backing;
	}

	case FUSE_READDIR | FUSE_POSTFILTER: {
		int backing = 0;

		if (strcmp(ctx->name, "/") == 0)
			backing = FUSE_BPF_USER_FILTER | FUSE_BPF_BACKING |
				  FUSE_BPF_POST_FILTER;

		bpf_printk("Paul: readdir postfilter %s %d", ctx->name, backing);
		return backing;
	}

	default:
		return 0;
	}
}

SEC("test_daemon")

/* return 1 to use backing fs, 0 to pass to usermode */
int trace2(struct bpf_fuse_data *ctx)
{
	switch (ctx->fuse_opcode) {
	case FUSE_LOOKUP: {
		/* real and partial use backing file */
		int backing = strcmp(ctx->name, "real") == 0 ||
			strcmp(ctx->name, "partial") == 0 ||
			strcmp(ctx->name, "MAILPATH") == 0;

		bpf_printk("Paul: lookup %s %d", ctx->name, backing);
		return backing ? 1 : 0;
	}

	case FUSE_GETATTR: {
		/* real and partial use backing file */
		int backing = strcmp(ctx->name, "/") == 0 ||
			strcmp(ctx->name, "real") == 0 ||
			strcmp(ctx->name, "partial") == 0;

		bpf_printk("Paul: getattr %s %d", ctx->name, backing);
		return backing ? 1 : 0;
	}

	case FUSE_OPEN: {
		int backing = 0;

		if (strcmp(ctx->name, "real") == 0)
			backing = 1;

		else if (strcmp(ctx->name, "partial") == 0)
			backing = 2;

		bpf_printk("Paul: open %s %d", ctx->name, backing);
		return backing;
	}

	case FUSE_OPENDIR: {
		int backing = 0;

		if (strcmp(ctx->name, "/") == 0)
			backing = 1;

		bpf_printk("Paul: opendir %s %d", ctx->name, backing);
		return backing;
	}

	case FUSE_READDIR: {
		int backing = 0;

		if (strcmp(ctx->name, "/") == 0)
			backing = FUSE_BPF_USER_FILTER | FUSE_BPF_BACKING |
				  FUSE_BPF_POST_FILTER;

		bpf_printk("Paul: readdir %s %d", ctx->name, backing);
		return backing;
	}

	case FUSE_READDIR | FUSE_POSTFILTER: {
		int backing = 0;

		if (strcmp(ctx->name, "/") == 0)
			backing = FUSE_BPF_USER_FILTER | FUSE_BPF_BACKING |
				  FUSE_BPF_POST_FILTER;

		bpf_printk("Paul: readdir postfilter %s %d", ctx->name, backing);
		return backing;
	}

	case FUSE_READ: {
		bpf_printk("Paul: read %llu %llu",
			   ctx->file_handle, ctx->offset);
		if (ctx->file_handle == 1 && ctx->offset == 0)
			return 0;
		return 1;
	}

	case FUSE_ACCESS: {
		bpf_printk("Paul: access");
		return 1;
	}

	default:
		return 0;
	}
}
