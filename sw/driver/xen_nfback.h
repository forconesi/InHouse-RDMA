#include "nf10.h"

extern int xen_nfback_init(struct nf10_adapter *adapter);
extern void xen_nfback_fini(void);
extern bool xenvif_connected(unsigned long domid);
extern int xenvif_start_xmit(unsigned long domid, struct desc *desc);
