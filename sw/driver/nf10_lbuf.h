#ifndef _NF10_LBUF_H
#define _NF10_LBUF_H

/* NR_LBUF is dependent on lbuf DMA engine */
#define NR_LBUF		2

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/pci.h>
#include "nf10.h"

/* offset to bar2 address of the card */
/* RX */
#define RX_LBUF_ADDR_BASE	1	/* 8B: [0]0x08 [1]0x10 */
#define RX_LBUF_STAT_BASE	6	/* 4B: [0]0x18 [1]0x1c */
#define rx_addr_off(i)	(RX_LBUF_ADDR_BASE + (i))
#define rx_stat_off(i)	(RX_LBUF_STAT_BASE + (i))
#define RX_READY		1
/* TX */
#define TX_LBUF_ADDR_BASE	5	/* 8B: [0]0x28 [1]0x30 */
#define TX_LBUF_STAT_BASE	11	/* 4B: [0]0x2C [1]0x34 <-XXX not 0x30, current HW decides it */ 
#define tx_addr_off(i)	(TX_LBUF_ADDR_BASE + (i))
#define tx_stat_off(i)	(TX_LBUF_STAT_BASE + (i << 1))	/* XXX: current HW overlaps some regions, will be fixed later on */
#define TX_LBUF_INTR_CTRL	9

#define TX	0
#define RX	1

struct nf10_adapter;
extern void nf10_lbuf_set_hw_ops(struct nf10_adapter *adapter);
#endif

#endif
