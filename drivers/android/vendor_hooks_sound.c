// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sound vendor hook tracepoint definitions.
 *
 * Keep these separate from vendor_hooks.c. The sound hook prototypes
 * introduce HDA types to Genksyms; defining them in their own translation
 * unit prevents those declarations from changing CRCs of existing hooks.
 */

#define CREATE_TRACE_POINTS
#include <trace/hooks/vendor_hooks.h>
#include <linux/tracepoint.h>

#include <trace/hooks/hda.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_hda_add_controls);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_hda_pcm_fill_silence);
