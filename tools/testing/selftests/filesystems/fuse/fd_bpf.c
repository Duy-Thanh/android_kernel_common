// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
// Copyright (c) 2021 Google LLC

#define __EXPORTED_HEADERS__
#define __KERNEL__

#include <uapi/linux/bpf.h>
#include <uapi/linux/fuse.h>

struct fuse_bpf_map {
	int map_type;
	size_t key_size;
	size_t value_size;
	int max_entries;
};

static void *(*bpf_map_lookup_elem)(struct fuse_bpf_map *map, void *key)
	= (void *) 1;

static void *(*bpf_map_update_elem)(struct fuse_bpf_map *map, void *key,
				    void *value, int flags)
	= (void *) 2;

static long (*bpf_trace_printk)(const char *fmt, __u32 fmt_size, ...)
	= (void *) 6;

static long (*bpf_get_current_pid_tgid)()
	= (void *) 14;

static long (*bpf_get_current_uid_gid)()
	= (void *) 15;

#define bpf_printk(fmt, ...)					\
	({			                                \
		char ____fmt[] = fmt;                           \
		bpf_trace_printk(____fmt, sizeof(____fmt),      \
		                 ##__VA_ARGS__);                \
	})

inline const void *fa_verify_in(struct __bpf_fuse_args *fa, int i, unsigned int size)
{
	const char *val = (void *)(long) fa->in_args[i].value;
	const char *end = (void *)(long) fa->in_args[i].end_offset;

	if (i >= fa->in_numargs)
		return NULL;
	if (val + size <= end)
		return val;
	return NULL;
}

inline void *fa_verify_out(struct __bpf_fuse_args *fa, int i, unsigned int size)
{
	char *val = (void *)(long) fa->out_args[i].value;
	char *end = (void *)(long) fa->out_args[i].end_offset;

	if (i >= fa->out_numargs)
		return NULL;
	if (val + size <= end)
		return val;
	return NULL;
}

#define SEC(NAME) __attribute__((section(NAME), used))

SEC("dummy")

inline int strcmp(const char *a, const char *b)
{
	int i;

	for (i = 0; i < __builtin_strlen(b) + 1; ++i)
		if (a[i] != b[i])
			return -1;

	return 0;
}

SEC("maps") struct fuse_bpf_map test_map = {
	BPF_MAP_TYPE_ARRAY,
	sizeof(uint32_t),
	sizeof(uint32_t),
	1000,
};

SEC("maps") struct fuse_bpf_map test_map2 = {
	BPF_MAP_TYPE_HASH,
	sizeof(uint32_t),
	sizeof(uint64_t),
	76,
};

SEC("test_daemon")

int trace_daemon(struct __bpf_fuse_args *fa)
{
	uint64_t uid_gid = bpf_get_current_uid_gid();
	uint32_t uid = uid_gid & 0xffffffff;
	uint64_t pid_tgid = bpf_get_current_pid_tgid();
	uint32_t pid = pid_tgid & 0xffffffff;
	uint32_t key = 23;
	uint32_t *pvalue;


	pvalue = bpf_map_lookup_elem(&test_map, &key);
	if (pvalue) {
		uint32_t value = *pvalue;

		bpf_printk("pid %u uid %u value %u", pid, uid, value);
		value++;
		bpf_map_update_elem(&test_map, &key,  &value, BPF_ANY);
	}

	switch (fa->opcode) {
	case FUSE_ACCESS | FUSE_PREFILTER: {
		bpf_printk("Access: %d", fa->nodeid);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_GETATTR | FUSE_PREFILTER: {
		const struct fuse_getattr_in *fgi = fa_verify_in(fa, 0, sizeof(*fgi));

		if (!fgi)
			return -1;

		bpf_printk("Get Attr %d", fgi->fh);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_SETATTR | FUSE_PREFILTER: {
		const struct fuse_setattr_in *fsi = fa_verify_in(fa, 0, sizeof(*fsi));

		if (!fsi)
			return -1;

		bpf_printk("Set Attr %d", fsi->fh);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_OPENDIR | FUSE_PREFILTER: {
		bpf_printk("Open Dir: %d", fa->nodeid);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_READDIR | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("Read Dir: fh: %lu", fri->fh, fri->offset);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_LOOKUP | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;

		bpf_printk("Lookup: %lx %s", fa->nodeid, name);
		if (fa->nodeid == 1)
			return BPF_FUSE_USER_PREFILTER;
		else
			return BPF_FUSE_CONTINUE;
	}

	case FUSE_MKNOD | FUSE_PREFILTER: {
		const struct fuse_mknod_in *fmi = fa_verify_in(fa, 0, sizeof(*fmi));
		const char *name = (void *)(long)fa->in_args[1].value;

		if (!fmi)
			return -1;

		bpf_printk("mknod %s %x %x", name,  fmi->rdev | fmi->mode, fmi->umask);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_MKDIR | FUSE_PREFILTER: {
		const struct fuse_mkdir_in *fmi = fa_verify_in(fa, 0, sizeof(*fmi));
		const char *name = (void *)(long)fa->in_args[1].value;

		if (!fmi)
			return -1;

		bpf_printk("mkdir: %s %x %x", name, fmi->mode, fmi->umask);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_RMDIR | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[0].value;

		bpf_printk("rmdir: %s", name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_RENAME | FUSE_PREFILTER: {
		const char *oldname = (void *)(long) fa->in_args[1].value;
		const char *newname = (void *)(long) fa->in_args[2].value;

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

		bpf_printk("unlink: %s", name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_LINK | FUSE_PREFILTER: {
		const struct fuse_link_in *fli = fa_verify_in(fa, 0, sizeof(*fli));
		const char *dst_name = (void *)(long) fa->in_args[1].value;

		if (!fli)
			return -1;

		bpf_printk("Link: %d %s", fli->oldnodeid, dst_name);
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

		bpf_printk("readlink from %s", link_name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_RELEASE | FUSE_PREFILTER: {
		const struct fuse_release_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("Release: %d", fri->fh);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_RELEASEDIR | FUSE_PREFILTER: {
		const struct fuse_release_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("Release Dir: %d", fri->fh);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_CREATE | FUSE_PREFILTER: {
		bpf_printk("Create %s", fa->in_args[1].value);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_OPEN | FUSE_PREFILTER: {
		bpf_printk("Open: %d", fa->nodeid);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_READ | FUSE_PREFILTER: {
		const struct fuse_read_in *fri = fa_verify_in(fa, 0, sizeof(*fri));

		if (!fri)
			return -1;

		bpf_printk("Read: fh: %lu, offset %lu, size %lu",
			   fri->fh, fri->offset, fri->size);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_WRITE | FUSE_PREFILTER: {
		const struct fuse_write_in *fwi = fa_verify_in(fa, 0, sizeof(*fwi));

		if (!fwi)
			return -1;

		bpf_printk("Write: fh: %lu, offset %lu, size %lu",
			   fwi->fh, fwi->offset, fwi->size);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_FLUSH | FUSE_PREFILTER: {
		const struct fuse_flush_in *ffi = fa_verify_in(fa, 0, sizeof(*ffi));

		if (!ffi)
			return -1;

		bpf_printk("Flush %d", ffi->fh);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_FALLOCATE | FUSE_PREFILTER: {
		const struct fuse_fallocate_in *ffa = fa_verify_in(fa, 0, sizeof(*ffa));

		if (!ffa)
			return -1;

		bpf_printk("Fallocate %d %lu", ffa->fh, ffa->length);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_GETXATTR | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[1].value;

		bpf_printk("Getxattr %d %s", fa->nodeid, name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_LISTXATTR | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[1].value;

		bpf_printk("Listxattr %d %s", fa->nodeid, name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_SETXATTR | FUSE_PREFILTER: {
		const char *name = (void *)(long)fa->in_args[1].value;

		bpf_printk("Setxattr %d %s", fa->nodeid, name);
		return BPF_FUSE_CONTINUE;
	}

	case FUSE_STATFS | FUSE_PREFILTER: {
		bpf_printk("statfs %d", fa->nodeid);
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
		if (fa->opcode & FUSE_PREFILTER)
			bpf_printk("prefilter *** UNKNOWN *** opcode: %d",
				   fa->opcode & FUSE_OPCODE_FILTER);
		else if (fa->opcode & FUSE_POSTFILTER)
			bpf_printk("postfilter *** UNKNOWN *** opcode: %d",
				   fa->opcode & FUSE_OPCODE_FILTER);
		else
			bpf_printk("*** UNKNOWN *** opcode: %d", fa->opcode);
		return BPF_FUSE_CONTINUE;
	}
}
