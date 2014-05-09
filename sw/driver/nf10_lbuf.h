#ifndef _NF10_LBUF_H
#define _NF10_LBUF_H

#include <linux/types.h>
#include <linux/pci.h>

struct desc {
	/* FIXME: one of pages and kern_addrs may not be needed */
	struct page	*page;
	void		*kern_addr;
	dma_addr_t	dma_addr;
};
/* NR_LBUF is dependent on lbuf DMA engine */
#define NR_LBUF		4
struct large_buffer {
	struct desc descs[NR_LBUF];
};

extern int nf10_lbuf_init(struct pci_dev *pdev);
extern void nf10_lbuf_free(struct pci_dev *pdev);

#endif
