// SPDX-License-Identifier: GPL-2.0-only
/*
 * In-kernel BPF loader helper for executing light skeleton loader programs
 * without exposing intermediate file descriptors to userspace.
 */

#include <linux/bpf.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/filter.h>
#include <linux/limits.h>
#include <linux/rcupdate_trace.h>
#include <linux/string.h>

#ifdef CONFIG_BPF_JIT
int bpf_loader_load_and_run(struct bpf_loader_opts *opts)
{
	struct bpf_prog *prog = NULL;
	struct bpf_map *map;
	union bpf_attr attr;
	u32 key = 0;
	int retval;
	int err;

	if (!bpf_capable())
		return -EPERM;

	if (!opts || !opts->insns || !opts->insn_cnt || !opts->data ||
	    !opts->data_sz || !opts->license || !opts->ctx)
		return -EINVAL;

	if (opts->ctx_sz == 0 || opts->ctx_sz > U16_MAX)
		return -EINVAL;

	/* 1. Allocate ephemeral loader map (no FD, no IDR entry) */
	memset(&attr, 0, sizeof(attr));
	attr.map_type = BPF_MAP_TYPE_ARRAY;
	attr.key_size = sizeof(u32);
	attr.value_size = opts->data_sz;
	attr.max_entries = 1;
	strscpy(attr.map_name, "__loader.map", sizeof(attr.map_name));

	map = bpf_map_create_kernel(&attr);
	if (IS_ERR(map))
		return PTR_ERR(map);

	/* 2. Populate map with loader data */
	err = map->ops->map_update_elem(map, &key, (void *)opts->data, 0);
	if (err)
		goto err_free_map;

	/* 3. Load loader program bound directly to map (no FD, no IDR entry) */
	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SYSCALL;
	attr.insns = (unsigned long)opts->insns;
	attr.insn_cnt = opts->insn_cnt;
	attr.license = (unsigned long)opts->license;
	attr.prog_flags = BPF_F_SLEEPABLE;
	strscpy(attr.prog_name, "__loader.prog", sizeof(attr.prog_name));

	prog = bpf_prog_load_kernel(&attr, &map, 1);
	if (IS_ERR(prog)) {
		err = PTR_ERR(prog);
		goto err_free_map;
	}

	/* 4. Execute loader program in current task context */
	if (opts->ctx_sz < prog->aux->max_ctx_offset) {
		err = -EINVAL;
		goto err_put_prog;
	}

	rcu_read_lock_trace();
	retval = bpf_prog_run_pin_on_cpu(prog, opts->ctx);
	rcu_read_unlock_trace();
	if (retval < 0)
		err = IS_ERR_VALUE((unsigned long)(long)retval) ? retval : -EINVAL;
	else
		err = 0;

err_put_prog:
	bpf_prog_put(prog);
err_free_map:
	bpf_map_put_with_uref(map);
	return err;
}
#else /* !CONFIG_BPF_JIT */
int bpf_loader_load_and_run(struct bpf_loader_opts *opts)
{
	return -EOPNOTSUPP;
}
#endif
EXPORT_SYMBOL_NS(bpf_loader_load_and_run, "BPF_INTERNAL");
