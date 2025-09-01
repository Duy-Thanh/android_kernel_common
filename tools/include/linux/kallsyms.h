/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBLOCKDEP_LINUX_KALLSYMS_H_
#define _LIBLOCKDEP_LINUX_KALLSYMS_H_

#include <linux/kernel.h>
#include <stdio.h>
#include <unistd.h>

#define KSYM_NAME_LEN 512

struct module;

static inline const char *kallsyms_lookup(unsigned long addr,
					  unsigned long *symbolsize,
					  unsigned long *offset,
					  char **modname, char *namebuf)
{
	return NULL;
}

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#ifdef HAVE_BACKTRACE_SUPPORT
#include <execinfo.h>
#include <stdlib.h>
static inline void print_ip_sym(const char *loglvl, unsigned long ip)
{
	char **name;

	name = backtrace_symbols((void **)&ip, 1);

	dprintf(STDOUT_FILENO, "%s\n", *name);

	free(name);
}
#else
static inline void print_ip_sym(const char *loglvl, unsigned long ip) {}
#endif
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
#include <execinfo.h>
#include <stdlib.h>
static inline void print_ip_sym(const char *loglvl, unsigned long ip)
{
	char **name;

	name = backtrace_symbols((void **)&ip, 1);

	dprintf(STDOUT_FILENO, "%s\n", *name);

	free(name);
}
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

#endif
