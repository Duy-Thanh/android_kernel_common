// SPDX-License-Identifier: GPL-2.0-only

#include <linux/types.h>
#include <kunit/visibility.h>
#include <asm/sysreg.h>
#include <asm/uaccess.h>
#include <asm/irqflags.h>

#include "binder_internal.h"

void binder_rust_write_daif(u64 val);
void binder_rust_write_ttbr0_el1(u64 val);
void binder_rust_write_ttbr1_el1(u64 val);
bool binder_rust_uaccess_ttbr0_enable(void);
bool binder_rust_uaccess_ttbr0_disable(void);
unsigned long binder_rust_arch_local_irq_save(void);
void binder_rust_arch_local_irq_restore(unsigned long flags);
void binder_rust_arch_local_irq_enable(void);
void binder_rust_arch_local_irq_disable(void);

void binder_rust_write_daif(u64 val)
{
	write_sysreg(val, daif);
}

void binder_rust_write_ttbr0_el1(u64 val)
{
	write_sysreg(val, ttbr0_el1);
}

void binder_rust_write_ttbr1_el1(u64 val)
{
	write_sysreg(val, ttbr1_el1);
}

bool binder_rust_uaccess_ttbr0_enable(void)
{
	return uaccess_ttbr0_enable();
}

bool binder_rust_uaccess_ttbr0_disable(void)
{
	return uaccess_ttbr0_disable();
}

unsigned long binder_rust_arch_local_irq_save(void)
{
	return arch_local_irq_save();
}

void binder_rust_arch_local_irq_restore(unsigned long flags)
{
	arch_local_irq_restore(flags);
}

void binder_rust_arch_local_irq_enable(void)
{
	arch_local_irq_enable();
}

void binder_rust_arch_local_irq_disable(void)
{
	arch_local_irq_disable();
}

EXPORT_SYMBOL_IF_KUNIT(binder_vm_fault);

static const char * const binder_return_strings[] = {
	"BR_ERROR",
	"BR_OK",
	"BR_TRANSACTION",
	"BR_REPLY",
	"BR_ACQUIRE_RESULT",
	"BR_DEAD_REPLY",
	"BR_TRANSACTION_COMPLETE",
	"BR_INCREFS",
	"BR_ACQUIRE",
	"BR_RELEASE",
	"BR_DECREFS",
	"BR_ATTEMPT_ACQUIRE",
	"BR_NOOP",
	"BR_SPAWN_LOOPER",
	"BR_FINISHED",
	"BR_DEAD_BINDER",
	"BR_CLEAR_DEATH_NOTIFICATION_DONE",
	"BR_FAILED_REPLY",
	"BR_FROZEN_REPLY",
	"BR_ONEWAY_SPAM_SUSPECT",
	"BR_TRANSACTION_PENDING_FROZEN",
	"BR_FROZEN_BINDER",
	"BR_CLEAR_FREEZE_NOTIFICATION_DONE",
};

static const char * const binder_command_strings[] = {
	"BC_TRANSACTION",
	"BC_REPLY",
	"BC_ACQUIRE_RESULT",
	"BC_FREE_BUFFER",
	"BC_INCREFS",
	"BC_ACQUIRE",
	"BC_RELEASE",
	"BC_DECREFS",
	"BC_INCREFS_DONE",
	"BC_ACQUIRE_DONE",
	"BC_ATTEMPT_ACQUIRE",
	"BC_REGISTER_LOOPER",
	"BC_ENTER_LOOPER",
	"BC_EXIT_LOOPER",
	"BC_REQUEST_DEATH_NOTIFICATION",
	"BC_CLEAR_DEATH_NOTIFICATION",
	"BC_DEAD_BINDER_DONE",
	"BC_TRANSACTION_SG",
	"BC_REPLY_SG",
	"BC_REQUEST_FREEZE_NOTIFICATION",
	"BC_CLEAR_FREEZE_NOTIFICATION",
	"BC_FREEZE_NOTIFICATION_DONE",
};

#define CREATE_TRACE_POINTS
#include "binder_trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(binder_transaction_received);
