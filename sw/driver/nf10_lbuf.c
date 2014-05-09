#include "nf10.h"
#include "nf10_lbuf.h"

#define LBUF_SIZE	HPAGE_PMD_SIZE
static inline struct page *alloc_lbuf(void)
{
	return alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
}

static inline void free_lbuf(struct page *page)
{
	__free_pages(page, HPAGE_PMD_ORDER);
}

static void unmap_and_free_lbuf(struct pci_dev *pdev, struct desc *desc, int rx)
{
	pci_unmap_single(pdev, desc->dma_addr, LBUF_SIZE,
			rx ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
	free_lbuf(desc->page);
}

static int alloc_and_map_lbuf(struct pci_dev *pdev, struct desc *desc, int rx)
{
retry:
	if ((desc->page = alloc_lbuf()) == NULL)
		return -ENOMEM;

	desc->kern_addr = page_address(desc->page);
	desc->dma_addr = pci_map_single(pdev, desc->kern_addr,
			LBUF_SIZE, rx ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(pdev, desc->dma_addr)) {
		free_lbuf(desc->page);
		return -EIO;
	}

	/* FIXME: due to the current HW constraint, dma bus address should
	 * not be within 32bit address space. So, this fixup will be removed */
	if ((desc->dma_addr >> 32) == 0) {
		unmap_and_free_lbuf(pdev, desc, rx);
		goto retry;
	}

	return 0;
}

static int __nf10_lbuf_init(struct pci_dev *pdev, int rx)
{
	struct nf10_adapter *adapter = pci_get_drvdata(pdev);
	struct large_buffer *lbuf = &adapter->lbuf;
	int i;
	int err = 0;

	for (i = 0; i < NR_LBUF; i++) {
		struct desc *desc = &lbuf->descs[rx][i];
		if ((err = alloc_and_map_lbuf(pdev, desc, rx)))
			break;
		netif_info(adapter, probe, adapter->netdev,
			   "%s lbuf[%d] is allocated at kern_addr=%p/dma_addr=%p"
			   " (size=%lu bytes)\n", rx ? "RX" : "TX", i, 
			   desc->kern_addr, (void *)desc->dma_addr, LBUF_SIZE);
	}
	

	if (unlikely(err))	/* failed to allocate all lbufs */
		for (i--; i >= 0; i--)
			unmap_and_free_lbuf(pdev, &lbuf->descs[rx][i], rx);

	return err;
}

static void __nf10_lbuf_free(struct pci_dev *pdev, int rx)
{
	struct nf10_adapter *adapter = pci_get_drvdata(pdev);
	struct large_buffer *lbuf = &adapter->lbuf;
	int i;

	for (i = 0; i < NR_LBUF; i++) {
		unmap_and_free_lbuf(pdev, &lbuf->descs[rx][i], rx);
		netif_info(adapter, drv, adapter->netdev,
			   "%s lbuf[%d] is freed from kern_addr=%p",
			   rx ? "RX" : "TX", i, lbuf->descs[rx][i].kern_addr);
	}
}

static void __nf10_lbuf_prepare_rx(struct pci_dev *pdev)
{
	struct nf10_adapter *adapter = pci_get_drvdata(pdev);
	struct large_buffer *lbuf = &adapter->lbuf;

	int i;
	for (i = 0; i < NR_LBUF; i++) {
		nf10_writeq(adapter, rx_addr_off(i),
			    lbuf->descs[RX][i].dma_addr);
		nf10_writel(adapter, rx_stat_off(i), RX_READY);
	}

	netif_info(adapter, drv, adapter->netdev, 
		   "RX buffers are prepared (sent to nf10)\n");
}

/* following functions are used by nf10_main */
int nf10_lbuf_init(struct pci_dev *pdev)
{
	/* TODO: TX */

	return __nf10_lbuf_init(pdev, 1);	/* RX */
}

void nf10_lbuf_free(struct pci_dev *pdev)
{
	/* TODO: TX */

	__nf10_lbuf_free(pdev, 1);		/* RX */
}

void nf10_lbuf_prepare_rx(struct pci_dev *pdev)
{
	__nf10_lbuf_prepare_rx(pdev);
}
