#ifndef __ARM64_KVM_HYP_NVHE_TRACE_H
#define __ARM64_KVM_HYP_NVHE_TRACE_H

#include <nvhe/trace.h>

#include <linux/trace_events.h>
#include <linux/ring_buffer.h>

#include <asm/kvm_hyptrace.h>
#include <asm/kvm_hypevents_defs.h>
#include <asm/percpu.h>

#ifdef CONFIG_TRACING

struct hyp_buffer_page {
	struct list_head list;
	struct buffer_data_page *page;
	atomic_t write;
	atomic_t entries;
};

#define HYP_RB_NONWRITABLE	0
#define HYP_RB_WRITABLE		1
#define HYP_RB_WRITING		2

struct hyp_rb_per_cpu {
	struct hyp_buffer_page *tail_page;
	struct hyp_buffer_page *reader_page;
	struct hyp_buffer_page *head_page;
	struct hyp_buffer_page *bpages;
	unsigned long nr_pages;
	atomic64_t write_stamp;
	atomic_t pages_touched;
	atomic_t nr_entries;
	atomic_t status;
	atomic_t overrun;
};

struct hyp_rb_per_cpu *rb_this_cpu(void);
void *rb_reserve_trace_entry(struct hyp_rb_per_cpu *cpu_buffer,
			     unsigned long length);
void rb_release_trace_entry(struct hyp_rb_per_cpu *cpu_buffer);
int register_hyp_event_ids(unsigned long start, unsigned long end);

#undef HYP_EVENT
#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)		\
	HYP_EVENT_FORMAT(__name, __struct);					\
	extern atomic_t __name##_enabled;					\
	extern struct hyp_event_id hyp_event_id_##__name;			\
	static inline void trace_##__name(__proto)				\
	{									\
		size_t length = sizeof(struct trace_hyp_format_##__name);	\
		struct hyp_rb_per_cpu *rb = rb_this_cpu();			\
		struct trace_hyp_format_##__name *__entry;			\
										\
		if (!atomic_read(&__name##_enabled))				\
			return;							\
		__entry = rb_reserve_trace_entry(rb, length);			\
		if (!__entry)							\
			return;							\
		__entry->hdr.id = hyp_event_id_##__name.id;			\
		__assign							\
		rb_release_trace_entry(rb);					\
	}

/* TODO: atomic_t to static_branch */

int __pkvm_load_tracing(unsigned long pack_va, size_t pack_size);
void __pkvm_teardown_tracing(void);
int __pkvm_enable_tracing(bool enable);
int __pkvm_rb_swap_reader_page(int cpu);
int __pkvm_rb_update_footers(int cpu);
int __pkvm_enable_event(unsigned short id, bool enable);

#else
static inline struct hyp_rb_per_cpu *rb_this_cpu(void) { return NULL; }

static inline void *rb_reserve_trace_entry(struct hyp_rb_per_cpu *cpu_buffer,
					   unsigned long length)
{
	return NULL;
}

static inline void rb_release_trace_entry(struct hyp_rb_per_cpu *cpu_buffer) { }

static inline int register_hyp_event_ids(unsigned long start, unsigned long end)
{
	return -ENODEV;
}

static inline int __pkvm_load_tracing(unsigned long pack_va, size_t pack_size)
{
	return -ENODEV;
}

static inline void __pkvm_teardown_tracing(void) { }

static inline int __pkvm_enable_tracing(bool enable) { return -ENODEV; }

static inline int __pkvm_rb_swap_reader_page(int cpu)
{
	return -ENODEV;
}

static inline int __pkvm_rb_update_footers(int cpu)
{
	return -ENODEV;
}

#undef HYP_EVENT
#define HYP_EVENT(__name, __proto, __struct, __assign, __printk)	\
	static inline void trace_##__name(__proto) {}

static inline int __pkvm_enable_event(unsigned short id, bool enable)
{
	return -ENODEV;
}
#endif
#endif
