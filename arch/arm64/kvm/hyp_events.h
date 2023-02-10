/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ARM64_KVM_HYP_EVENTS_H__
#define __ARM64_KVM_HYP_EVENTS_H__

struct hyp_event;
struct hyp_event_id;

#ifdef CONFIG_TRACING
int kvm_hyp_init_events(void);
void kvm_hyp_init_events_tracefs(struct dentry *parent);
int kvm_hyp_init_mod_events(struct hyp_event *event,
			    int nr_hyp_events,
			    struct hyp_event_id *event_id,
			    int nr_hyp_event_ids);
#else
static inline int kvm_hyp_init_events(void) { return 0; }
static inline void kvm_hyp_init_events_tracefs(struct dentry *parent) { }
static inline int kvm_hyp_init_mod_events(struct hyp_event *event,
					  int nr_hyp_events,
					  struct hyp_event_id *event_id,
					  int nr_hyp_event_ids)
{
	return 0;
}
#endif
#endif
