/*
 * Copyright (c) 2026 Justin Hibbits <justin@hibbits.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _FSL_DMA_H
#define _FSL_DMA_H

#include <sys/types.h>
#include <sys/bus.h>

/* Max transfer size -- 64MB */
#define FSL_DMA_MAXSIZE		0x3ffffffUL
#define FSL_DMA_BOUNDARY 	0x400000000ULL
#define	FSL_DMA_MINSIZE 	32
#define	FSL_DMA_MAXSEG		0x03ffffff

#define	FSL_DMA_SATR_SREADTYPE_M		0x000f0000
#define	FSL_DMA_SATR_SREADTYPE_S		16
#define	FSL_DMA_SATR_SREADTYPE_NOSNOOP		0x4
#define	FSL_DMA_SATR_SREADTYPE_SNOOP_LOCAL	0x5
#define	FSL_DMA_SATR_SREADTYPE_UNLOCK_L2	0x7
#define	FSL_DMA_DATR_DWRITETYPE_M		0x000f0000
#define	FSL_DMA_DATR_DWRITETYPE_S		16
#define	FSL_DMA_DATR_DWRITETYPE_NOSNOOP		0x4
#define	FSL_DMA_DATR_DWRITETYPE_SNOOP_LOCAL	0x5
#define	FSL_DMA_DATR_DWRITETYPE_ALLOC_L2	0x6
#define	FSL_DMA_DATR_DWRITETYPE_LOCK_L2		0x7

struct dma_list_descriptor {
	uint64_t next;
	uint64_t link_phys;
	uint32_t src_stride;
	uint32_t dst_stride;
	uint32_t rsvd[2];
} __aligned(32);

struct dma_link_descriptor {
	uint32_t src_attr;
	uint32_t src_addr;
	uint32_t dst_attr;
	uint32_t dst_addr;
	uint64_t next;
	uint32_t byte_cnt;
	uint32_t rsvd;
} __aligned(32);

/* 0 in any field disables it */
struct fsl_dma_ch_conf {
	uint8_t		src_hold_count;
	uint8_t		dest_hold_count;
	uint32_t	period;
	uint32_t	(*ih)(void *);
	void		*ih_user;
};

int fsl_dma_ch_start(device_t dev, struct dma_link_descriptor *desc);
int fsl_dma_ch_stop(device_t dev);
int fsl_dma_ch_configure(device_t dev, struct fsl_dma_ch_conf *conf);

#endif /* _FSL_DMA_H */
