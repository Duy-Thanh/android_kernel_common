/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_HYPEVISOR_H
#define __LINUX_HYPEVISOR_H

/*
 *	Generic Hypervisor support
 *		Juergen Gross <jgross@suse.com>
 */

#ifdef CONFIG_X86

#include <asm/jailhouse_para.h>
#include <asm/x86_init.h>

static inline void hypervisor_pin_vcpu(int cpu)
{
	x86_platform.hyper.pin_vcpu(cpu);
}

#else /* !CONFIG_X86 */

#include <linux/of.h>

static inline void hypervisor_pin_vcpu(int cpu)
{
}

static inline bool jailhouse_paravirt(void)
{
	return of_find_compatible_node(NULL, NULL, "jailhouse,cell");
}

#endif /* !CONFIG_X86 */

static inline bool hypervisor_isolated_pci_functions(void)
{
	if (IS_ENABLED(CONFIG_S390))
		return true;

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
	if (IS_ENABLED(CONFIG_LOONGARCH))
		return true;

||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
	return jailhouse_paravirt();
}

#endif /* __LINUX_HYPEVISOR_H */
