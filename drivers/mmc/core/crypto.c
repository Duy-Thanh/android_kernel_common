// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

/*
 * MMC inline crypto functions.
 */

#include "crypto.h"
#include "queue.h"

#include <linux/blk-crypto.h>
#include <linux/blkdev.h>
#include <linux/keyslot-manager.h>
#include <linux/mmc/host.h>

void mmc_crypto_setup_rq_keyslot_manager(struct mmc_host *host,
					 struct request_queue *q)
{
	if (host->caps2 & MMC_CAP2_CRYPTO)
		q->ksm = host->ksm;
}

void mmc_crypto_destroy_host(struct mmc_host *host)
{
	keyslot_manager_destroy(host->ksm);
}

void mmc_crypto_prepare_request(struct mmc_queue_req *mqrq)
{
	struct request *rq = mmc_queue_req_to_req(mqrq);
	struct mmc_request *mrq = &mqrq->brq.mrq;
	const struct bio_crypt_ctx *bc;

	if (!bio_crypt_should_process(rq)) {
		mrq->crypto_key_slot = -1;
		return;
	}

	bc = rq->bio->bi_crypt_context;
	mrq->crypto_key_slot = bc->bc_keyslot;
	mrq->data_unit_num = bc->bc_dun[0];
	mrq->crypto_key = bc->bc_key;
}
