#ifndef _NF10_LBUF_H
#define _NF10_LBUF_H

/* NR_LBUF is dependent on lbuf DMA engine */
#define NR_LBUF		2

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/pci.h>
#include "nf10.h"

/* offset to bar2 address of the card */
#define RX_LBUF_ADDR_BASE	1
#define RX_LBUF_STAT_BASE	6
#define rx_addr_off(i)	(RX_LBUF_ADDR_BASE + (i))
#define rx_stat_off(i)	(RX_LBUF_STAT_BASE + (i))
#define RX_READY		1

#define TX	0
#define RX	1

struct nf10_adapter;
extern void nf10_lbuf_set_hw_ops(struct nf10_adapter *adapter);
#endif

#endif
