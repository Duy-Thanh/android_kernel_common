/*
 * Copyright (C) 2016 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef GOLDFISH_DMA_H
#define GOLDFISH_DMA_H

#include <uapi/linux/goldfish/goldfish_dma.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/bug.h>

struct goldfish_dma_context {
	struct device *pdev_dev;	/* pointer to feed to dma_*_coherent */
	void *dma_vaddr;		/* kernel vaddr of dma region */
	size_t dma_size;		/* size of dma region */
	dma_addr_t phys_begin;		/* paddr of dma region */
	dma_addr_t phys_end;		/* paddr of dma region + dma_size */
};

#endif /* GOLDFISH_DMA_H */

