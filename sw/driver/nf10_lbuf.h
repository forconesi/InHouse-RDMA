#ifndef _NF10_LBUF_H
#define _NF10_LBUF_H

/* NR_LBUF is dependent on lbuf DMA engine */
#define NR_LBUF		2

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/pci.h>
#include "nf10.h"

/* offset to bar2 address of the card */
#define RX_LBUF_ADDR_BASE	0x40
#define RX_LBUF_STAT_BASE	0x60
#define RX_READY		0x1
#define TX_LBUF_ADDR_BASE	0x80
#define TX_LBUF_STAT_BASE	0xA0
#define TX_COMPLETION_ADDR	0xB0
#define TX_INTR_CTRL_ADDR	0xB8
#define TX_COMPLETION_SIZE	((NR_LBUF << 2) + 8)	/* DWORD for each desc + QWORD (last gc addr) */
#define TX_LAST_GC_ADDR_OFFSET	(NR_LBUF << 2)		/* last gc addr following completion buffers for all descs */
#define TX_COMPLETION_OKAY	0xcacabeef

#define rx_addr_off(i)	(RX_LBUF_ADDR_BASE + (i << 3))
#define rx_stat_off(i)	(RX_LBUF_STAT_BASE + (i << 2))
#define tx_addr_off(i)	(TX_LBUF_ADDR_BASE + (i << 3))
#define tx_stat_off(i)	(TX_LBUF_STAT_BASE + (i << 2))

#define TX	0
#define RX	1

struct nf10_adapter;
extern void nf10_lbuf_set_hw_ops(struct nf10_adapter *adapter);
#endif

#endif
