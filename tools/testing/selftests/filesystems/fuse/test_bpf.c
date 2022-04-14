// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google LLC

#define __EXPORTED_HEADERS__

#include <uapi/linux/types.h>
#include <uapi/linux/fuse.h>
#include <uapi/linux/errno.h>

#include <stdbool.h>

#define SEC(NAME) __attribute__((section(NAME), used))

static long (*bpf_trace_printk)(const char *fmt, __u32 fmt_size, ...)
	= (void *) 6;

#define bpf_printk(fmt, ...)					\
	({			                                \
		char ____fmt[] = fmt;                           \
		bpf_trace_printk(____fmt, sizeof(____fmt),      \
		                 ##__VA_ARGS__);                \
	})

inline const void *fa_verify_in(struct fuse_bpf_args *fa, int i, unsigned int size)
{
	const char *val = fa->in_args[i].value;
	const char *end = fa->in_args[i].end_offset;

	if (i >= fa->in_numargs)
		return NULL;
	if (val + size <= end)
		return val;
	return NULL;
}

inline void *fa_verify_out(struct fuse_bpf_args *fa, int i, unsigned int size)
{
	char *val = fa->out_args[i].value;
	char *end = fa->out_args[i].end_offset;

	if (i >= fa->out_numargs)
		return NULL;
	if (val + size <= end)
		return val;
	return NULL;
}

SEC("dummy")

inline int strcmp(const char *a, const char *b)
{
	int i;

	for (i = 0; i < __builtin_strlen(b) + 1; ++i)
		if (a[i] != b[i])
			return -1;
	return 0;
}

/* This is a macro to enforce inlining. Without it, the compiler will do the wrong thing for bpf */
#define strcmp_check(a, b, end_b) \
		(((b) + __builtin_strlen(a) + 1 > (end_b)) ? -1 : strcmp((b), (a)))

SEC("test_readdir_redact")
/* return FUSE_BPF_BACKING to use backing fs, 0 to pass to usermode */
int readdir_test(struct fuse_bpf_args *fa)
{
	switch (fa->opcode) {
	case FUSE_READDIR | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("readdir %d", fri->fh);
		return FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER;
	}

	case FUSE_READDIR | FUSE_POSTFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("readdir postfilter %x", fri->fh);
		return FUSE_BPF_USER_FILTER;
	}

	default:
		bpf_printk("opcode %d", fa->opcode);
		return FUSE_BPF_BACKING;
	}
}
SEC("test_trace")

/* return FUSE_BPF_BACKING to use backing fs, 0 to pass to usermode */
int trace_test(struct fuse_bpf_args *fa)
{
	switch (fa->opcode) {
	case FUSE_LOOKUP | FUSE_PREFILTER: {
		/* real and partial use backing file */
		const char *name = fa->in_args[0].value;
		const char *end = fa->in_args[0].end_offset;
		bool backing = false;

		if (strcmp_check("real", name, end) == 0 || strcmp_check("partial", name, end) == 0)
			backing = true;

		if (strcmp_check("dir", name, end) == 0)
			backing = true;
		if (strcmp_check("dir2", name, end) == 0)
			backing = true;

		if (strcmp_check("file1", name, end) == 0)
			backing = true;
		if (strcmp_check("file2", name, end) == 0)
			backing = true;

		bpf_printk("lookup %s %d", name, backing);
		return backing ? FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER : 0;
	}

	case FUSE_LOOKUP | FUSE_POSTFILTER: {
		const char *name = fa->in_args[0].value;
		const char *end = fa->in_args[0].end_offset;
		struct fuse_entry_out *feo = fa_verify_out(fa, 0, sizeof(*feo));

		if (!feo)
			return -1;

		if (strcmp_check("real", name, end) == 0)
			feo->nodeid = 5;
		else if (strcmp_check("partial", name, end) == 0)
			feo->nodeid = 6;

		bpf_printk("post-lookup %s %d", name, feo->nodeid);
		return 0;
	}

	case FUSE_ACCESS | FUSE_PREFILTER: {
		bpf_printk("Access: %d", fa->nodeid);
		return FUSE_BPF_BACKING;
	}

	case FUSE_CREATE | FUSE_PREFILTER:
		bpf_printk("Create: %d", fa->nodeid);
		return FUSE_BPF_BACKING;

	case FUSE_MKNOD | FUSE_PREFILTER: {
		const struct fuse_mknod_in *fmi = fa_verify_in(fa, 0, sizeof(*fmi));
		const char *name = fa->in_args[1].value;

		if (!fmi)
			return -1;

		bpf_printk("mknod %s %x %x", name, fmi->rdev | fmi->mode, fmi->umask);
		return FUSE_BPF_BACKING;
	}

	case FUSE_MKDIR | FUSE_PREFILTER: {
		const struct fuse_mkdir_in *fmi = fa_verify_in(fa, 0, sizeof(*fmi));
		const char *name = fa->in_args[1].value;

		if (!fmi)
			return -1;

		bpf_printk("mkdir %s %x %x", name, fmi->mode, fmi->umask);
		return FUSE_BPF_BACKING;
	}

	case FUSE_RMDIR | FUSE_PREFILTER: {
		const char *name = fa->in_args[0].value;

		bpf_printk("rmdir %s", name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_RENAME | FUSE_PREFILTER: {
		const char *oldname = fa->in_args[1].value;
		const char *newname = fa->in_args[2].value;

		bpf_printk("rename from %s", oldname);
		bpf_printk("rename to %s", newname);
		return FUSE_BPF_BACKING;
	}

	case FUSE_RENAME2 | FUSE_PREFILTER: {
		const struct fuse_rename2_in *fri = fa_verify_in(fa, 0, sizeof(*fri));
		uint32_t flags = fri->flags;
		const char *oldname = fa->in_args[1].value;
		const char *newname = fa->in_args[2].value;

		if (!fri)
			return -1;

		bpf_printk("rename(%x) from %s", flags, oldname);
		bpf_printk("rename to %s", newname);
		return FUSE_BPF_BACKING;
	}

	case FUSE_UNLINK | FUSE_PREFILTER: {
		const char *name = fa->in_args[0].value;

		bpf_printk("unlink %s", name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_LINK | FUSE_PREFILTER: {
		const struct fuse_link_in *fli = fa_verify_in(fa, 0, sizeof(*fli));
		const char *link_name = fa->in_args[1].value;

		if (!fli)
			return -1;

		bpf_printk("link %d %s", fli->oldnodeid, link_name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_SYMLINK | FUSE_PREFILTER: {
		const char *link_name = fa->in_args[0].value;
		const char *link_dest = fa->in_args[1].value;

		bpf_printk("symlink from %s", link_name);
		bpf_printk("symlink to %s", link_dest);
		return FUSE_BPF_BACKING;
	}

	case FUSE_READLINK | FUSE_PREFILTER: {
		const char *link_name = fa->in_args[0].value;

		bpf_printk("readlink from", link_name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_OPEN | FUSE_PREFILTER: {
		int backing = 0;

		switch (fa->nodeid) {
		case 5:
			backing = FUSE_BPF_BACKING;
			break;

		case 6:
			backing = FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER;
			break;

		default:
			break;
		}

		bpf_printk("open %d %d", fa->nodeid, backing);
		return backing;
	}

	case FUSE_OPEN | FUSE_POSTFILTER:
		bpf_printk("open postfilter");
		return FUSE_BPF_USER_FILTER;

	case FUSE_READ | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("read %llu %llu", fri->fh, fri->offset);
		if (fri->fh == 1 && fri->offset == 0)
			return 0;
		return FUSE_BPF_BACKING;
	}

	case FUSE_GETATTR | FUSE_PREFILTER: {
		/* real and partial use backing file */
		int backing = 0;

		switch (fa->nodeid) {
		case 1:
		case 5:
		case 6:
		/*
		 * TODO: Find better solution
		 * Add 100 to stop clang compiling to jump table which bpf hates
		 */
		case 100:
			backing = FUSE_BPF_BACKING;
			break;
		}

		bpf_printk("getattr %d %d", fa->nodeid, backing);
		return backing;
	}

	case FUSE_SETATTR | FUSE_PREFILTER: {
		/* real and partial use backing file */
		int backing = 0;

		switch (fa->nodeid) {
		case 1:
		case 5:
		case 6:
		/* TODO See above */
		case 100:
			backing = FUSE_BPF_BACKING;
			break;
		}

		bpf_printk("setattr %d %d", fa->nodeid, backing);
		return backing;
	}

	case FUSE_OPENDIR | FUSE_PREFILTER: {
		int backing = 0;

		switch (fa->nodeid) {
		case 1:
			backing = FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER;
			break;
		}

		bpf_printk("opendir %d %d", fa->nodeid, backing);
		return backing;
	}

	case FUSE_OPENDIR | FUSE_POSTFILTER: {
		struct fuse_open_out *foo = fa_verify_out(fa, 0, sizeof(*foo));

		if (!foo)
			return -1;

		foo->fh = 2;
		bpf_printk("opendir postfilter");
		return 0;
	}

	case FUSE_READDIR | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));
		int backing = 0;

		if (!fri)
			return -1;

		if (fri->fh == 2)
			backing = FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER;

		bpf_printk("readdir %d %d", fri->fh, backing);
		return backing;
	}

	case FUSE_READDIR | FUSE_POSTFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));
		int backing = 0;

		if (!fri)
			return -1;

		if (fri->fh == 2)
			backing = FUSE_BPF_USER_FILTER | FUSE_BPF_BACKING |
				  FUSE_BPF_POST_FILTER;

		bpf_printk("readdir postfilter %d %d", fri->fh, backing);
		return backing;
	}

	case FUSE_FLUSH | FUSE_PREFILTER: {
		const struct fuse_flush_in *ffi = fa_verify_in(fa, 0, sizeof(*ffi));

		if (!ffi)
			return -1;

		bpf_printk("Flush %d", ffi->fh);
		return FUSE_BPF_BACKING;
	}

	case FUSE_GETXATTR | FUSE_PREFILTER: {
		const struct fuse_flush_in *ffi = fa_verify_in(fa, 0, sizeof(*ffi));
		const char *name = fa->in_args[1].value;

		if (!ffi)
			return -1;

		bpf_printk("getxattr %d %s", ffi->fh, name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_LISTXATTR | FUSE_PREFILTER: {
		const struct fuse_flush_in *ffi = fa_verify_in(fa, 0, sizeof(*ffi));
		const char *name = fa->in_args[1].value;

		if (!ffi)
			return -1;

		bpf_printk("listxattr %d %s", ffi->fh, name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_SETXATTR | FUSE_PREFILTER: {
		const struct fuse_setxattr_in *fsi = fa_verify_in(fa, 0, sizeof(*fsi));
		const char *name = fa->in_args[1].value;
		unsigned int size = fa->in_args[2].size;

		if (!fsi)
			return -1;
		bpf_printk("setxattr %x %s %u", fsi->flags, name, size);
		return FUSE_BPF_BACKING;
	}

	case FUSE_REMOVEXATTR | FUSE_PREFILTER: {
		const char *name = fa->in_args[0].value;

		bpf_printk("removexattr %s", name);
		return FUSE_BPF_BACKING;
	}

	case FUSE_CANONICAL_PATH | FUSE_PREFILTER: {
		bpf_printk("canonical_path");
		return FUSE_BPF_BACKING;
	}

	case FUSE_STATFS | FUSE_PREFILTER: {
		bpf_printk("statfs");
		return FUSE_BPF_BACKING;
	}

	case FUSE_LSEEK | FUSE_PREFILTER: {
		const struct fuse_lseek_in *fli = fa_verify_in(fa, 0, sizeof(*fli));

		if (!fli)
			return -1;
		bpf_printk("lseek type:%d, offset:%lld", fli->whence, fli->offset);
		return FUSE_BPF_BACKING;
	}

	default:
		bpf_printk("Unknown opcode %d", fa->opcode);
		return 0;
	}
}

SEC("test_hidden")

int trace_hidden(struct fuse_bpf_args *fa)
{
	switch (fa->opcode) {
	case FUSE_LOOKUP | FUSE_PREFILTER: {
		const char *name = fa->in_args[0].value;
		const char *end = fa->in_args[0].end_offset;

		bpf_printk("Lookup: %s", name);
		if (!strcmp_check("show", name, end))
			return FUSE_BPF_BACKING;
		if (!strcmp_check("hide", name, end))
			return -ENOENT;

		return FUSE_BPF_BACKING;
	}

	case FUSE_ACCESS | FUSE_PREFILTER: {
		bpf_printk("Access: %d", fa->nodeid);
		return FUSE_BPF_BACKING;
	}

	case FUSE_CREATE | FUSE_PREFILTER:
		bpf_printk("Create: %d", fa->nodeid);
		return FUSE_BPF_BACKING;

	case FUSE_WRITE | FUSE_PREFILTER:
	// TODO: Clang combines similar printk calls, causing BPF to complain
	//	bpf_printk("Write: %d", fa->nodeid);
		return FUSE_BPF_BACKING;

	case FUSE_FLUSH | FUSE_PREFILTER: {
	//	const struct fuse_flush_in *ffi = fa->in_args[0].value;

	//	bpf_printk("Flush %d", ffi->fh);
		return FUSE_BPF_BACKING;
	}

	case FUSE_RELEASE | FUSE_PREFILTER: {
	//	const struct fuse_release_in *fri = fa->in_args[0].value;

	//	bpf_printk("Release %d", fri->fh);
		return FUSE_BPF_BACKING;
	}

	case FUSE_FALLOCATE | FUSE_PREFILTER:
	//	bpf_printk("fallocate %d", fa->nodeid);
		return FUSE_BPF_BACKING;

	case FUSE_CANONICAL_PATH | FUSE_PREFILTER: {
		return FUSE_BPF_BACKING;
	}
	default:
		bpf_printk("Unknown opcode: %d", fa->opcode);
		return 0;
	}
}

SEC("test_simple")
int trace_simple(struct fuse_bpf_args *fa)
{
	if (fa->opcode & FUSE_PREFILTER)
		bpf_printk("prefilter opcode: %d",
			   fa->opcode & FUSE_OPCODE_FILTER);
	else if (fa->opcode & FUSE_POSTFILTER)
		bpf_printk("postfilter opcode: %d",
			   fa->opcode & FUSE_OPCODE_FILTER);
	else
		bpf_printk("*** UNKNOWN *** opcode: %d", fa->opcode);
	return FUSE_BPF_BACKING;
}

SEC("test_passthrough")
int trace_daemon(struct fuse_bpf_args *fa)
{
	switch (fa->opcode) {
	case FUSE_LOOKUP | FUSE_PREFILTER: {
		const char *name = fa->in_args[0].value;

		bpf_printk("Lookup prefilter: %lx %s", fa->nodeid, name);
		return FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER;
	}

	case FUSE_LOOKUP | FUSE_POSTFILTER: {
		const char *name = fa->in_args[0].value;
		struct fuse_entry_bpf_out *febo = fa_verify_out(fa, 1, sizeof(*febo));

		if (!febo)
			return -1;
		bpf_printk("Lookup postfilter: %lx %s %lu", fa->nodeid, name);
		febo->bpf_action = FUSE_ACTION_REMOVE;

		return FUSE_BPF_USER_FILTER;
	}

	default:
		if (fa->opcode & FUSE_PREFILTER)
			bpf_printk("prefilter opcode: %d",
				   fa->opcode & FUSE_OPCODE_FILTER);
		else if (fa->opcode & FUSE_POSTFILTER)
			bpf_printk("postfilter opcode: %d",
				   fa->opcode & FUSE_OPCODE_FILTER);
		else
			bpf_printk("*** UNKNOWN *** opcode: %d", fa->opcode);
		return FUSE_BPF_BACKING;
	}
}

SEC("test_error")

/* return FUSE_BPF_BACKING to use backing fs, 0 to pass to usermode */
int error_test(struct fuse_bpf_args *fa)
{
	switch (fa->opcode) {
	case FUSE_MKDIR | FUSE_PREFILTER: {
		bpf_printk("mkdir");
		return FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER;
	}
	case FUSE_MKDIR | FUSE_POSTFILTER: {
		bpf_printk("mkdir postfilter");
		if (fa->error_in == -EEXIST)
			return -EPERM;

		return 0;
	}

	case FUSE_LOOKUP | FUSE_PREFILTER: {
		const char *name = fa->in_args[0].value;

		bpf_printk("lookup prefilter %s", name);
		return FUSE_BPF_BACKING | FUSE_BPF_POST_FILTER;
	}
	case FUSE_LOOKUP | FUSE_POSTFILTER: {
		const char *name = fa->in_args[0].value;
		const char *end = fa->in_args[0].end_offset;

		bpf_printk("lookup postfilter %s %d", name, fa->error_in);
		if (strcmp_check("doesnotexist", name, end) == 0/* && fa->error_in == -EEXIST*/) {
			bpf_printk("lookup postfilter doesnotexist");
			return FUSE_BPF_USER_FILTER;
		}
		bpf_printk("meh");
		return 0;
	}

	default:
		if (fa->opcode & FUSE_PREFILTER)
			bpf_printk("prefilter opcode: %d",
				   fa->opcode & FUSE_OPCODE_FILTER);
		else if (fa->opcode & FUSE_POSTFILTER)
			bpf_printk("postfilter opcode: %d",
				   fa->opcode & FUSE_OPCODE_FILTER);
		else
			bpf_printk("*** UNKNOWN *** opcode: %d", fa->opcode);
		return FUSE_BPF_BACKING;
	}
}

SEC("test_verify")

int verify_test(struct fuse_bpf_args *fa)
{
	if (fa->opcode == (FUSE_MKDIR | FUSE_PREFILTER)) {
		const char *start;
		const char *end;
		const struct fuse_mkdir_in *in;

		start = fa->in_args[0].value;
		end = fa->in_args[0].end_offset;
		if (start + sizeof(*in) <= end) {
			in = (struct fuse_mkdir_in *)(start);
			bpf_printk("test1: %d %d", in->mode, in->umask);
		}

		return FUSE_BPF_BACKING;
	}
	return FUSE_BPF_BACKING;
}

SEC("test_verify_fail")

int verify_fail_test(struct fuse_bpf_args *fa)
{
	struct t {
		uint32_t a;
		uint32_t b;
		char d[];
	};
	if (fa->opcode == (FUSE_MKDIR | FUSE_PREFILTER)) {
		const char *start;
		const char *end;
		const struct t *c;

		start = fa->in_args[0].value;
		end = fa->in_args[0].end_offset;
		if (start + sizeof(struct t) <= end) {
			c = (struct t *)start;
			bpf_printk("test1: %d %d %d", c->a, c->b, c->d[0]);
		}
		return FUSE_BPF_BACKING;
	}
	return FUSE_BPF_BACKING;
}

SEC("test_verify_fail2")

int verify_fail_test2(struct fuse_bpf_args *fa)
{
	if (fa->opcode == (FUSE_MKDIR | FUSE_PREFILTER)) {
		const char *start;
		const char *end;
		struct fuse_mkdir_in *c;

		start = fa->in_args[0].value;
		end = fa->in_args[1].end_offset;
		if (start + sizeof(*c) <= end) {
			c = (struct fuse_mkdir_in *)start;
			bpf_printk("test1: %d %d", c->mode, c->umask);
		}
		return FUSE_BPF_BACKING;
	}
	return FUSE_BPF_BACKING;
}
