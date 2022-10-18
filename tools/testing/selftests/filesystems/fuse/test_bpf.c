// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google LLC

#include <linux/fuse.h>
#include <linux/errno.h>
#include <uapi/linux/bpf.h>
#include <linux/types.h>

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

//static long (*bpf_fuse_get_writeable_in)(struct fuse_bpf_args *fa, u32 index, void *value,
//					 u64 size, bool copy)
//	= (void *) 156;
static long (*bpf_fuse_get_writeable_out)(struct __bpf_fuse_args *fa, u32 index, void *value,
					  u64 size, bool copy)
	= (void *) 157;


//#define bpf_make_writable_in(fa, index, size, copy)\
//	(void *)bpf_fuse_get_writeable_in(fa, index, size, copy)
#define bpf_make_writable_out(fa, index, value, size, copy) \
	(void *)bpf_fuse_get_writeable_out(fa, index, (void *)(long)value, size, copy)

inline const void *fa_verify_in(struct __bpf_fuse_args *fa, int i, unsigned int size)
{
	const char *val = (void *)(long)fa->in_args[i].value;
	const char *end = (void *)(long)fa->in_args[i].end_offset;

	if (i >= fa->in_numargs)
		return NULL;
	if (val + size <= end)
		return val;
	return NULL;
}

inline void *fa_verify_out(struct __bpf_fuse_args *fa, int i, unsigned int size)
{
	char *val = (void *)(long)fa->out_args[i].value;
	char *end = (void *)(long)fa->out_args[i].end_offset;

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
/* return BPF_FUSE_CONTINUE to use backing fs, BPF_FUSE_USER to pass to usermode */
int readdir_test(struct __bpf_fuse_args *fa)
{
	switch (fa->opcode) {
	case FUSE_READDIR | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("readdir %d", fri->fh);
		return BPF_FUSE_POSTFILTER;
	}

	case FUSE_READDIR | FUSE_POSTFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("readdir postfilter %x", fri->fh);
		return BPF_FUSE_USER_POSTFILTER;
	}

	default:
		bpf_printk("opcode %d", fa->opcode);
		return BPF_FUSE_CONTINUE;
	}
}
SEC("test_trace")

/* return BPF_FUSE_CONTINUE to use backing fs, BPF_FUSE_USER to pass to usermode */
int trace_test(struct __bpf_fuse_args *fa)
{
	switch (fa->opcode) {
	case FUSE_LOOKUP | FUSE_PREFILTER: {
		/* real and partial use backing file */
		const char *name = (void *)(long)fa->in_args[0].value;
		const char *end = (void *)(long)fa->in_args[0].end_offset;
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
		return backing ? BPF_FUSE_POSTFILTER : BPF_FUSE_USER;
	}

	case FUSE_LOOKUP | FUSE_POSTFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;
		const char *end = (void *)(long)fa->in_args[0].end_offset;
		struct fuse_entry_out *feo = (struct fuse_entry_out *)
				bpf_make_writable_out(fa, 0, fa->out_args[0].value,
						      sizeof(*feo), true);

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
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_CREATE | FUSE_PREFILTER:
		bpf_printk("Create: %d", fa->nodeid);
		return BPF_FUSE_CONTINUE;

	case FUSE_MKNOD | FUSE_PREFILTER: {
		const struct fuse_mknod_in *fmi = fa_verify_in(fa, 0, sizeof(*fmi));
		const char *name = (void *)(long)fa->in_args[1].value;

		if (!fmi)
			return -1;

		bpf_printk("mknod %s %x %x", name, fmi->rdev | fmi->mode, fmi->umask);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_MKDIR | FUSE_PREFILTER: {
		const struct fuse_mkdir_in *fmi = fa_verify_in(fa, 0, sizeof(*fmi));
		const char *name = (void *)(long)fa->in_args[1].value;

		if (!fmi)
			return -1;

		bpf_printk("mkdir %s %x %x", name, fmi->mode, fmi->umask);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_RMDIR | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;

		bpf_printk("rmdir %s", name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_RENAME | FUSE_PREFILTER: {
		const char *oldname = (void *)(long)fa->in_args[1].value;
		const char *newname = (void *)(long)fa->in_args[2].value;

		bpf_printk("rename from %s", oldname);
		bpf_printk("rename to %s", newname);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_RENAME2 | FUSE_PREFILTER: {
		const struct fuse_rename2_in *fri = fa_verify_in(fa, 0, sizeof(*fri));
		uint32_t flags = fri->flags;
		const char *oldname = (void *)(long) fa->in_args[1].value;
		const char *newname = (void *)(long) fa->in_args[2].value;

		if (!fri)
			return -1;

		bpf_printk("rename(%x) from %s", flags, oldname);
		bpf_printk("rename to %s", newname);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_UNLINK | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;

		bpf_printk("unlink %s", name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_LINK | FUSE_PREFILTER: {
		const struct fuse_link_in *fli = fa_verify_in(fa, 0, sizeof(*fli));
		const char *link_name = (void *)(long) fa->in_args[1].value;

		if (!fli)
			return -1;

		bpf_printk("link %d %s", fli->oldnodeid, link_name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_SYMLINK | FUSE_PREFILTER: {
		const char *link_name = (void *)(long) fa->in_args[0].value;
		const char *link_dest = (void *)(long) fa->in_args[1].value;

		bpf_printk("symlink from %s", link_name);
		bpf_printk("symlink to %s", link_dest);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_READLINK | FUSE_PREFILTER: {
		const char *link_name = (void *)(long) fa->in_args[0].value;

		bpf_printk("readlink from", link_name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_OPEN | FUSE_PREFILTER: {
		int backing = BPF_FUSE_USER;

		switch (fa->nodeid) {
		case 5:
			backing = BPF_FUSE_CONTINUE;
			break;

		case 6:
			backing = BPF_FUSE_POSTFILTER;
			break;

		default:
			break;
		}

		bpf_printk("open %d %d", fa->nodeid, backing);
		return backing;
	}

	case FUSE_OPEN | FUSE_POSTFILTER:
		bpf_printk("open postfilter");
		return BPF_FUSE_USER_POSTFILTER;

	case FUSE_READ | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("read %llu %llu", fri->fh, fri->offset);
		if (fri->fh == 1 && fri->offset == 0)
			return BPF_FUSE_USER;
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_GETATTR | FUSE_PREFILTER: {
		/* real and partial use backing file */
		int backing = BPF_FUSE_USER;

		switch (fa->nodeid) {
		case 1:
		case 5:
		case 6:
		/*
		 * TODO: Find better solution
		 * Add 100 to stop clang compiling to jump table which bpf hates
		 */
		case 100:
			backing = BPF_FUSE_CONTINUE;
			break;
		}

		bpf_printk("getattr %d %d", fa->nodeid, backing);
		return backing;
	}

	case FUSE_SETATTR | FUSE_PREFILTER: {
		/* real and partial use backing file */
		int backing = BPF_FUSE_USER;

		switch (fa->nodeid) {
		case 1:
		case 5:
		case 6:
		/* TODO See above */
		case 100:
			backing = BPF_FUSE_CONTINUE;
			break;
		}

		bpf_printk("setattr %d %d", fa->nodeid, backing);
		return backing;
	}

	case FUSE_OPENDIR | FUSE_PREFILTER: {
		int backing = BPF_FUSE_USER;

		switch (fa->nodeid) {
		case 1:
			backing = BPF_FUSE_POSTFILTER;
			break;
		}

		bpf_printk("opendir %d %d", fa->nodeid, backing);
		return backing;
	}

	case FUSE_OPENDIR | FUSE_POSTFILTER: {
		struct fuse_open_out *foo = bpf_make_writable_out(fa, 0, fa->out_args[0].value,
								  sizeof(*foo), true);

		if (!foo)
			return -1;

		foo->fh = 2;
		bpf_printk("opendir postfilter");
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_READDIR | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));
		int backing = BPF_FUSE_USER;

		if (!fri)
			return -1;

		if (fri->fh == 2)
			backing = BPF_FUSE_POSTFILTER;

		bpf_printk("readdir %d %d", fri->fh, backing);
		return backing;
	}

	case FUSE_READDIR | FUSE_POSTFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));
		int backing = BPF_FUSE_CONTINUE;

		if (!fri)
			return -1;

		if (fri->fh == 2)
			backing = BPF_FUSE_USER_POSTFILTER;

		bpf_printk("readdir postfilter %d %d", fri->fh, backing);
		return backing;
	}

	case FUSE_FLUSH | FUSE_PREFILTER: {
		const struct fuse_flush_in *ffi = fa_verify_in(fa, 0, sizeof(*ffi));

		if (!ffi)
			return -1;

		bpf_printk("Flush %d", ffi->fh);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_GETXATTR | FUSE_PREFILTER: {
		const struct fuse_getxattr_in *fgi = fa_verify_in(fa, 0, sizeof(*fgi));
		const char *name = (void *)(long)fa->in_args[1].value;

		if (!fgi)
			return -1;

		bpf_printk("getxattr %d %s", fgi->size, name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_LISTXATTR | FUSE_PREFILTER: {
		const struct fuse_getxattr_in *fgi = fa_verify_in(fa, 0, sizeof(*fgi));
		const char *name = (void *)(long)fa->in_args[1].value;

		if (!fgi)
			return -1;

		bpf_printk("listxattr %d %s", fgi->size, name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_SETXATTR | FUSE_PREFILTER: {
		const struct fuse_setxattr_in *fsi = fa_verify_in(fa, 0, sizeof(*fsi));
		const char *name = (void *)(long)fa->in_args[1].value;
		unsigned int size = fa->in_args[2].size;

		if (!fsi)
			return -1;
		bpf_printk("setxattr %x %s %u", fsi->flags, name, size);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_REMOVEXATTR | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;

		bpf_printk("removexattr %s", name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_CANONICAL_PATH | FUSE_PREFILTER: {
		bpf_printk("canonical_path");
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_STATFS | FUSE_PREFILTER: {
		bpf_printk("statfs");
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_LSEEK | FUSE_PREFILTER: {
		const struct fuse_lseek_in *fli = fa_verify_in(fa, 0, sizeof(*fli));

		if (!fli)
			return -1;
		bpf_printk("lseek type:%d, offset:%lld", fli->whence, fli->offset);
		return BPF_FUSE_CONTINUE;
	}

	default:
		bpf_printk("Unknown opcode %d", fa->opcode);
		return BPF_FUSE_USER;
	}
}

SEC("test_hidden")

int trace_hidden(struct __bpf_fuse_args *fa)
{
	switch (fa->opcode) {
	case FUSE_LOOKUP | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;
		const char *end = (void *)(long)fa->in_args[0].end_offset;

		bpf_printk("Lookup: %s", name);
		if (!strcmp_check("show", name, end))
			return BPF_FUSE_CONTINUE;
		if (!strcmp_check("hide", name, end))
			return -ENOENT;

		return BPF_FUSE_CONTINUE;
	}

	case FUSE_ACCESS | FUSE_PREFILTER: {
		bpf_printk("Access: %d", fa->nodeid);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_CREATE | FUSE_PREFILTER:
		bpf_printk("Create: %d", fa->nodeid);
		return BPF_FUSE_CONTINUE;

	case FUSE_WRITE | FUSE_PREFILTER:
	// TODO: Clang combines similar printk calls, causing BPF to complain
	//	bpf_printk("Write: %d", fa->nodeid);
		return BPF_FUSE_CONTINUE;

	case FUSE_FLUSH | FUSE_PREFILTER: {
	//	const struct fuse_flush_in *ffi = fa->in_args[0].value;

	//	bpf_printk("Flush %d", ffi->fh);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_RELEASE | FUSE_PREFILTER: {
	//	const struct fuse_release_in *fri = fa->in_args[0].value;

	//	bpf_printk("Release %d", fri->fh);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_FALLOCATE | FUSE_PREFILTER:
	//	bpf_printk("fallocate %d", fa->nodeid);
		return BPF_FUSE_CONTINUE;

	case FUSE_CANONICAL_PATH | FUSE_PREFILTER: {
		return BPF_FUSE_CONTINUE;
	}
	default:
		bpf_printk("Unknown opcode: %d", fa->opcode);
		return BPF_FUSE_CONTINUE;
	}
}

SEC("test_simple")
int trace_simple(struct __bpf_fuse_args *fa)
{
	if (fa->opcode & FUSE_PREFILTER)
		bpf_printk("prefilter opcode: %d",
			   fa->opcode & FUSE_OPCODE_FILTER);
	else if (fa->opcode & FUSE_POSTFILTER)
		bpf_printk("postfilter opcode: %d",
			   fa->opcode & FUSE_OPCODE_FILTER);
	else
		bpf_printk("*** UNKNOWN *** opcode: %d", fa->opcode);
	return BPF_FUSE_CONTINUE;
}

SEC("test_passthrough")
int trace_daemon(struct __bpf_fuse_args *fa)
{
	switch (fa->opcode) {
	case FUSE_LOOKUP | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;

		bpf_printk("Lookup prefilter: %lx %s", fa->nodeid, name);
		return BPF_FUSE_POSTFILTER;
	}

	case FUSE_LOOKUP | FUSE_POSTFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;
		struct fuse_entry_bpf_out *febo = bpf_make_writable_out(fa, 1,
									fa->out_args[1].value,
									sizeof(*febo), true);

		if (!febo)
			return -1;
		bpf_printk("Lookup postfilter: %lx %s %lu", fa->nodeid, name);
		febo->bpf_action = FUSE_ACTION_REMOVE;

		return BPF_FUSE_USER_POSTFILTER;
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
		return BPF_FUSE_CONTINUE;
	}
}

SEC("test_error")

/* return BPF_FUSE_CONTINUE to use backing fs, BPF_FUSE_USER to pass to usermode */
int error_test(struct __bpf_fuse_args *fa)
{
	switch (fa->opcode) {
	case FUSE_MKDIR | FUSE_PREFILTER: {
		bpf_printk("mkdir");
		return BPF_FUSE_POSTFILTER;
	}
	case FUSE_MKDIR | FUSE_POSTFILTER: {
		bpf_printk("mkdir postfilter");
		if (fa->error_in == -EEXIST)
			return -EPERM;

		return 0;
	}

	case FUSE_LOOKUP | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;

		bpf_printk("lookup prefilter %s", name);
		return BPF_FUSE_POSTFILTER;
	}
	case FUSE_LOOKUP | FUSE_POSTFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;
		const char *end = (void *)(long)fa->in_args[0].end_offset;

		bpf_printk("lookup postfilter %s %d", name, fa->error_in);
		if (strcmp_check("doesnotexist", name, end) == 0/* && fa->error_in == -EEXIST*/) {
			bpf_printk("lookup postfilter doesnotexist");
			return BPF_FUSE_USER_POSTFILTER;
		}

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
		return BPF_FUSE_CONTINUE;
	}
}


SEC("test_readdirplus")
int readdirplus_test(struct __bpf_fuse_args *fa)
{
	switch (fa->opcode) {
	case FUSE_READDIR | FUSE_PREFILTER: {
		return BPF_FUSE_USER;
	}
	}
	return BPF_FUSE_CONTINUE;
}

SEC("test_verify")

int verify_test(struct __bpf_fuse_args *fa)
{
	if (fa->opcode == (FUSE_MKDIR | FUSE_PREFILTER)) {
		const char *start;
		const char *end;
		const struct fuse_mkdir_in *in;

		start = (void *)(long) fa->in_args[0].value;
		end = (void *)(long) fa->in_args[0].end_offset;
		if (start + sizeof(*in) <= end) {
			in = (struct fuse_mkdir_in *)(start);
			bpf_printk("test1: %d %d", in->mode, in->umask);
		}

		return BPF_FUSE_CONTINUE;
	}
	return BPF_FUSE_CONTINUE;
}

SEC("test_verify_fail")

int verify_fail_test(struct __bpf_fuse_args *fa)
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

		start = (void *)(long) fa->in_args[0].value;
		end = (void *)(long) fa->in_args[0].end_offset;
		if (start + sizeof(struct t) <= end) {
			c = (struct t *)start;
			bpf_printk("test1: %d %d %d", c->a, c->b, c->d[0]);
		}
		return BPF_FUSE_CONTINUE;
	}
	return BPF_FUSE_CONTINUE;
}

SEC("test_verify_fail2")

int verify_fail_test2(struct __bpf_fuse_args *fa)
{
	if (fa->opcode == (FUSE_MKDIR | FUSE_PREFILTER)) {
		const char *start;
		const char *end;
		struct fuse_mkdir_in *c;

		start = (void *)(long) fa->in_args[0].value;
		end = (void *)(long) fa->in_args[1].end_offset;
		if (start + sizeof(*c) <= end) {
			c = (struct fuse_mkdir_in *)start;
			bpf_printk("test1: %d %d", c->mode, c->umask);
		}
		return BPF_FUSE_CONTINUE;
	}
	return BPF_FUSE_CONTINUE;
}

SEC("test_verify_fail3")
/* Cannot write directly to fa */
int verify_fail_test3(struct __bpf_fuse_args *fa)
{
	if (fa->opcode == (FUSE_LOOKUP | FUSE_POSTFILTER)) {
		const char *name = (void *)(long)fa->in_args[0].value;
		const char *end = (void *)(long)fa->in_args[0].end_offset;
		struct fuse_entry_out *feo = fa_verify_out(fa, 0, sizeof(*feo));

		if (!feo)
			return -1;

		if (strcmp_check("real", name, end) == 0)
			feo->nodeid = 5;
		else if (strcmp_check("partial", name, end) == 0)
			feo->nodeid = 6;

		bpf_printk("post-lookup %s %d", name, feo->nodeid);
		return BPF_FUSE_CONTINUE;
	}
	return BPF_FUSE_CONTINUE;
}

SEC("test_verify_fail4")
/* Cannot write outside of requested area */
int verify_fail_test4(struct __bpf_fuse_args *fa)
{
	if (fa->opcode == (FUSE_LOOKUP | FUSE_POSTFILTER)) {
		const char *name = (void *)(long)fa->in_args[0].value;
		const char *end = (void *)(long)fa->in_args[0].end_offset;
		struct fuse_entry_out *feo = bpf_make_writable_out(fa, 0, fa->out_args[0].value,
								   1, true);

		if (!feo)
			return -1;

		if (strcmp_check("real", name, end) == 0)
			feo->nodeid = 5;
		else if (strcmp_check("partial", name, end) == 0)
			feo->nodeid = 6;

		bpf_printk("post-lookup %s %d", name, feo->nodeid);
		return BPF_FUSE_CONTINUE;
	}
	return BPF_FUSE_CONTINUE;
}

SEC("test_verify_fail5")
/* Cannot use old verification after requesting writable */
int verify_fail_test5(struct __bpf_fuse_args *fa)
{
	if (fa->opcode == (FUSE_LOOKUP | FUSE_POSTFILTER)) {
		struct fuse_entry_out *feo;
		struct fuse_entry_out *feo_w;

		feo = fa_verify_out(fa, 0, sizeof(*feo));
		if (!feo)
			return -1;

		feo_w = bpf_make_writable_out(fa, 0, fa->out_args[0].value, sizeof(*feo_w), true);
		bpf_printk("post-lookup %d", feo->nodeid);
		if (!feo_w)
			return -1;

		feo_w->nodeid = 5;

		return BPF_FUSE_CONTINUE;
	}
	return BPF_FUSE_CONTINUE;
}

SEC("test_verify5")
/* Can use new verification after requesting writable */
int verify_pass_test5(struct __bpf_fuse_args *fa)
{
	if (fa->opcode == (FUSE_LOOKUP | FUSE_POSTFILTER)) {
		struct fuse_entry_out *feo;
		struct fuse_entry_out *feo_w;

		feo = fa_verify_out(fa, 0, sizeof(*feo));
		if (!feo)
			return -1;

		bpf_printk("post-lookup %d", feo->nodeid);

		feo_w = bpf_make_writable_out(fa, 0, fa->out_args[0].value, sizeof(*feo_w), true);

		feo = fa_verify_out(fa, 0, sizeof(*feo));
		if (feo)
			bpf_printk("post-lookup %d", feo->nodeid);
		if (!feo_w)
			return -1;

		feo_w->nodeid = 5;

		return BPF_FUSE_CONTINUE;
	}
	return BPF_FUSE_CONTINUE;
}

SEC("test_verify_fail6")
/* Reading context from a nonsense offset is not allowed */
int verify_pass_test6(struct __bpf_fuse_args *fa)
{
	char *nonsense = (char *)fa;

	bpf_printk("post-lookup %d", nonsense[1]);

	return BPF_FUSE_CONTINUE;
}

SEC("test_lookup_postfilter")
int lookuppostfilter_test(struct __bpf_fuse_args *fa)
{
	switch(fa->opcode) {
	case FUSE_LOOKUP | FUSE_PREFILTER:
		return BPF_FUSE_POSTFILTER;
	case FUSE_LOOKUP | FUSE_POSTFILTER:
		return BPF_FUSE_USER_POSTFILTER;
	default:
		return BPF_FUSE_CONTINUE;
	}
}
