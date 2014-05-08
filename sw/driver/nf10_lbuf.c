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

static void unmap_and_free_lbuf(struct pci_dev *pdev, struct desc *desc)
{
	pci_unmap_single(pdev, desc->dma_addr, LBUF_SIZE, PCI_DMA_FROMDEVICE);
	free_lbuf(desc->page);
}

static int alloc_and_map_lbuf(struct pci_dev *pdev, struct desc *desc)
{
	int nr_try = 0;
retry:
	if ((desc->page = alloc_lbuf()) == NULL)
		return -ENOMEM;

	desc->kern_addr = page_address(desc->page);
	desc->dma_addr = pci_map_single(pdev, desc->kern_addr,
			LBUF_SIZE, PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(pdev, desc->dma_addr)) {
		free_lbuf(desc->page);
		return -EIO;
	}

	/* FIXME: due to the current HW constraint, dma bus address should
	 * not be within 32bit address space. So, this fixup will be removed */
	if ((desc->dma_addr >> 32) == 0) {
		unmap_and_free_lbuf(pdev, desc);

		if (nr_try++ < 10) {	/* for safety */
			pr_err("failed to alloc lbuf over 32bit bus space\n");
			return -EIO;
		}

		goto retry;
	}
	return 0;
}

int nf10_lbuf_init(struct pci_dev *pdev)
{
	struct nf10_adapter *adapter = pci_get_drvdata(pdev);
	struct large_buffer *lbuf = &adapter->lbuf;
	int i;
	int err = 0;

	for (i = 0; i < NR_LBUF; i++)
		if ((err = alloc_and_map_lbuf(pdev, &lbuf->descs[i])))
			break;

	if (unlikely(err))	/* failed to allocate all lbufs */
		for (i--; i >= 0; i--)
			unmap_and_free_lbuf(pdev, &lbuf->descs[i]);

	return err;
}
