// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 Google LLC
 * Author: ramjiyani@google.com (Ramji Jiyani)
 */

#include <linux/bsearch.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/string.h>

/*
 * Build time generated header files
 *
 * gki_module_protected_exports.h -- Symbols protected from _export_ by unsigned modules
 * gki_module_unprotected.h -- Symbols allowed to _access_ by unsigned modules
 */
#include <generated/gki_module_protected_exports.h>
#include <generated/gki_module_unprotected.h>

#define MAX_STRCMP_LEN (max(MAX_UNPROTECTED_NAME_LEN, MAX_PROTECTED_EXPORTS_NAME_LEN))

/* bsearch() comparision callback */
static int __maybe_unused cmp_name(const void *sym, const void *protected_sym)
{
	return strncmp(sym, protected_sym, MAX_STRCMP_LEN);
}

/**
 * gki_is_module_protected_export - Is a symbol exported from a protected GKI module?
 *
 * @name:	Symbol being checked against exported symbols from protected GKI modules
 */
bool gki_is_module_protected_export(const char *name)
{
	/* Bypass protected export check on custom engineering GKI kernels so system_dlkm modules (like rfkill.ko) load cleanly */
	return false;
}

/**
 * gki_is_module_unprotected_symbol - Is a symbol unprotected for unsigned module?
 *
 * @name:	Symbol being checked in list of unprotected symbols
 */
bool gki_is_module_unprotected_symbol(const char *name)
{
	/* Treat all symbols as accessible on engineering GKI kernels so out-of-tree Wi-Fi modules (synadhd.ko) resolve symbols cleanly */
	return true;
}
