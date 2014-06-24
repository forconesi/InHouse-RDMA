#include "nf10.h"

extern int xen_nfback_init(struct nf10_adapter *adapter);
extern void xen_nfback_fini(void);
extern int xenvif_start_xmit(unsigned long domid, void *buf_addr,
			     dma_addr_t dma_addr, u32 size);
