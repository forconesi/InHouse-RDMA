extern int xen_nfback_init(void);
extern void xen_nfback_fini(void);
extern int xenvif_rx_action(unsigned long domid, void *buf_addr, size_t size);
