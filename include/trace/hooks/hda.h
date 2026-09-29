/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hda

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_HDA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_HDA_H

#include <trace/hooks/vendor_hooks.h>

struct hda_codec;
struct snd_pcm_substream;

DECLARE_HOOK(android_vh_hda_add_controls,
	TP_PROTO(struct hda_codec *codec, u16 associated_nid, int *err),
	TP_ARGS(codec, associated_nid, err));

DECLARE_HOOK(android_vh_hda_pcm_fill_silence,
	TP_PROTO(struct snd_pcm_substream *substream, int channel,
		 unsigned long pos, unsigned long bytes, bool *handled),
	TP_ARGS(substream, channel, pos, bytes, handled));

#endif /* _TRACE_HOOK_HDA_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
