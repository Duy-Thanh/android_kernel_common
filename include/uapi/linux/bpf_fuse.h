#ifndef _UAPI__LINUX_BPF_FUSE_H__
#define _UAPI__LINUX_BPF_FUSE_H__

#include <uapi/linux/limits.h>

#define FUSE_BPF_USER_FILTER	1
#define FUSE_BPF_BACKING	2
#define FUSE_BPF_POST_FILTER	4
#define FUSE_BPF_ERROR		8

#define FUSE_POSTFILTER		0x20000

struct bpf_fuse_data {
	int fuse_opcode;

	union {
		struct { /* FUSE_LOOKUP, FUSE_OPEN */
			s8 name[NAME_MAX];
		};

		struct { /* FUSE_READ */
			u64 file_handle;
			u64 offset;
		};
	};
};

#endif /* _UAPI__LINUX_BPF_FUSE_H__ */
