#ifndef _NF10_LBUF_H
#define _NF10_LBUF_H

#include <linux/types.h>
#include <linux/pci.h>

/* NR_LBUF is dependent on lbuf DMA engine */
#define NR_LBUF		2

/* offset to bar2 address of the card */
#define RX_LBUF_ADDR_BASE	1
#define RX_LBUF_STAT_BASE	6
#define rx_addr_off(i)	(RX_LBUF_ADDR_BASE + (i))
#define rx_stat_off(i)	(RX_LBUF_STAT_BASE + (i))
#define RX_READY		1

#define TX	0
#define RX	1

struct desc {
	/* FIXME: one of pages and kern_addrs may not be needed */
	struct page	*page;
	void		*kern_addr;
	dma_addr_t	dma_addr;
};
struct large_buffer {
	struct desc descs[2][NR_LBUF];	/* 0=TX and 1=RX */
};

extern int nf10_lbuf_init(struct pci_dev *pdev);
extern void nf10_lbuf_free(struct pci_dev *pdev);
extern void nf10_lbuf_prepare_rx(struct pci_dev *pdev);

#endif
