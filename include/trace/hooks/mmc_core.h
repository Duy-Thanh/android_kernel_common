/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmc_core

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MMC_CORE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MMC_CORE_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct mmc_host;
struct sdhci_host;
struct mmc_card;
DECLARE_HOOK(android_vh_sdhci_get_cd,
	     TP_PROTO(struct sdhci_host *host, bool *allow),
	     TP_ARGS(host, allow));
DECLARE_HOOK(android_vh_sdhci_card_event,
	     TP_PROTO(struct sdhci_host *host),
	     TP_ARGS(host));
DECLARE_HOOK(android_vh_sd_update_bus_speed_mode,
	     TP_PROTO(struct mmc_card *card),
	     TP_ARGS(card));
DECLARE_HOOK(android_vh_mmc_send_app_op_cond,
	     TP_PROTO(struct mmc_host *host, u32 ocr, u32 rocr, int err),
	     TP_ARGS(host, ocr, rocr, err));
DECLARE_HOOK(android_vh_mmc_attach_sd,
	     TP_PROTO(struct mmc_host *host, int err),
	     TP_ARGS(host, err));
DECLARE_HOOK(android_vh_mmc_blk_reset,
	     TP_PROTO(struct mmc_host *host, int err, bool *allow),
	     TP_ARGS(host, err, allow));
DECLARE_HOOK(android_vh_mmc_blk_mq_rw_recovery,
	     TP_PROTO(struct mmc_card *card),
	     TP_ARGS(card));
DECLARE_HOOK(android_vh_mmc_rescan_try_freq,
	     TP_PROTO(struct mmc_host *host),
	     TP_ARGS(host));
DECLARE_HOOK(android_vh_mmc_alloc_host,
	     TP_PROTO(struct mmc_host *host),
	     TP_ARGS(host));
DECLARE_HOOK(android_vh_mmc_add_host,
	     TP_PROTO(struct mmc_host *host),
	     TP_ARGS(host));
DECLARE_HOOK(android_vh_mmc_remove_host,
	     TP_PROTO(struct mmc_host *host),
	     TP_ARGS(host));
DECLARE_HOOK(android_vh_mmc_start_host,
	     TP_PROTO(struct mmc_host *host),
	     TP_ARGS(host));
DECLARE_HOOK(android_vh_mmc_gpio_cd_irqt,
	     TP_PROTO(struct mmc_host *host, bool *allow),
	     TP_ARGS(host, allow));
#else
#define trace_android_vh_sdhci_get_cd(host, allow)
#define trace_android_vh_sdhci_card_event(host)
#define trace_android_vh_sd_update_bus_speed_mode(card)
#define trace_android_vh_mmc_send_app_op_cond(host, ocr, rocr, err)
#define trace_android_vh_mmc_attach_sd(host, err)
#define trace_android_vh_mmc_blk_reset(host, allow)
#define trace_android_vh_mmc_blk_mq_rw_recovery(host, allow)
#define trace_android_vh_mmc_rescan_try_freq(host)
#define trace_android_vh_mmc_alloc_host(host)
#define trace_android_vh_mmc_add_host(host)
#define trace_android_vh_mmc_remove_host(host)
#define trace_android_vh_mmc_start_host(host)
#define trace_android_vh_mmc_gpio_cd_irqt(host, allow)
#endif

#endif /* _TRACE_HOOK_MMC_CORE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

