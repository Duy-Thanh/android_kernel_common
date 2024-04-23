/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rwsem
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_RWSEM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_RWSEM_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct rw_semaphore;
struct rwsem_waiter;
DECLARE_HOOK(android_vh_rwsem_init,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_wake,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_write_finished,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_alter_rwsem_list_add,
	TP_PROTO(struct rwsem_waiter *waiter,
		 struct rw_semaphore *sem,
		 bool *already_on_list),
	TP_ARGS(waiter, sem, already_on_list));
DECLARE_HOOK(android_vh_rwsem_downgrade_wake_finish,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_wake_finish,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
<<<<<<< HEAD   (8835ecee98e5e6cc49238cd60e5df8243864ed02 ANDROID: GKI: Add initial empty desktop symbol list)

||||||| BASE   (943d6419f8c35a3918f2b9e7d1692920efbfe4e6 ANDROID: GKI: Update symbol list for vivo)
DECLARE_HOOK(android_vh_record_rwsem_reader_owned,
	TP_PROTO(struct rw_semaphore *sem,
		 struct list_head *wlist),
	TP_ARGS(sem, wlist));
DECLARE_HOOK(android_vh_clear_rwsem_reader_owned,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_record_rwsem_writer_owned,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_clear_rwsem_writer_owned,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
=======
DECLARE_HOOK(android_vh_record_rwsem_reader_owned,
	TP_PROTO(struct rw_semaphore *sem,
		 struct list_head *wlist),
	TP_ARGS(sem, wlist));
DECLARE_HOOK(android_vh_clear_rwsem_reader_owned,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_record_rwsem_writer_owned,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_clear_rwsem_writer_owned,
	TP_PROTO(struct rw_semaphore *sem),
	TP_ARGS(sem));
DECLARE_HOOK(android_vh_rwsem_read_trylock_failed,
	TP_PROTO(struct rw_semaphore *sem, long *cntp, int *ret),
	TP_ARGS(sem, cntp, ret));
>>>>>>> CHANGE (032603c88f6462c50a66c93f437954ecdb160e64 ANDROID: vendor_hooks: add hooks in rwsem read trylock)
#endif /* _TRACE_HOOK_RWSEM_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
