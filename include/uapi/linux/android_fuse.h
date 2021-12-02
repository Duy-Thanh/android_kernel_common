#ifndef _LINUX_ANDROID_FUSE_H
#define _LINUX_ANDROID_FUSE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/*
 * Fuse BPF Args
 *
 * Used to communicate with bpf programs to allow checking or altering certain values.
 * The end_offset allows the bpf verifier to check boundaries statically. This reflects
 * the ends of the buffer. size shows the length that was actually used.
 *
 */

/** One input argument of a request */
struct fuse_bpf_in_arg {
	uint32_t size;
	const void *value;
	const void *end_offset;
};

/** One output argument of a request */
struct fuse_bpf_arg {
	uint32_t size;
	void *value;
	void *end_offset;
};

#define FUSE_MAX_IN_ARGS 5
#define FUSE_MAX_OUT_ARGS 3

#define FUSE_BPF_FORCE (1 << 0)
#define FUSE_BPF_OUT_ARGVAR (1 << 6)

struct fuse_bpf_args {
	uint64_t nodeid;
	uint32_t opcode;
	uint32_t error_in;
	uint32_t in_numargs;
	uint32_t out_numargs;
	uint32_t flags;
	struct fuse_bpf_in_arg in_args[FUSE_MAX_IN_ARGS];
	struct fuse_bpf_arg out_args[FUSE_MAX_OUT_ARGS];
};

#define FUSE_BPF_USER_FILTER	1
#define FUSE_BPF_BACKING	2
#define FUSE_BPF_POST_FILTER	4

#define FUSE_OPCODE_FILTER	0x0ffff
#define FUSE_PREFILTER		0x10000
#define FUSE_POSTFILTER		0x20000

#endif  // _LINUX_ANDROID_FUSE_H
