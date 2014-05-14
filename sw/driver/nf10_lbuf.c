#include <linux/etherdevice.h>
#include "nf10.h"
#include "nf10_lbuf.h"

struct desc {
	/* FIXME: one of pages and kern_addrs may not be needed */
	struct page	*page;
	void		*kern_addr;
	dma_addr_t	dma_addr;
};

struct large_buffer {
	struct desc descs[2][NR_LBUF];	/* 0=TX and 1=RX */
	unsigned int tx_cons;
	unsigned int rx_cons;
};

static struct lbuf_hw {
	struct large_buffer lbuf;
} lbuf_hw;

#define get_lbuf()	(&lbuf_hw.lbuf)

#define LBUF_SIZE	HPAGE_PMD_SIZE

static inline struct page *alloc_lbuf(void)
{
	return alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
}

static inline void free_lbuf(struct page *page)
{
	__free_pages(page, HPAGE_PMD_ORDER);
}

static void unmap_and_free_lbuf(struct nf10_adapter *adapter,
				struct desc *desc, int rx)
{
	pci_unmap_single(adapter->pdev, desc->dma_addr, LBUF_SIZE,
			rx ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
	free_lbuf(desc->page);
}

static int alloc_and_map_lbuf(struct nf10_adapter *adapter,
			      struct desc *desc, int rx)
{
retry:
	if ((desc->page = alloc_lbuf()) == NULL)
		return -ENOMEM;

	desc->kern_addr = page_address(desc->page);
	desc->dma_addr = pci_map_single(adapter->pdev, desc->kern_addr,
			LBUF_SIZE, rx ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(adapter->pdev, desc->dma_addr)) {
		free_lbuf(desc->page);
		return -EIO;
	}

	/* FIXME: due to the current HW constraint, dma bus address should
	 * not be within 32bit address space. So, this fixup will be removed */
	if ((desc->dma_addr >> 32) == 0) {
		unmap_and_free_lbuf(adapter, desc, rx);
		goto retry;
	}

	return 0;
}

static int __nf10_lbuf_init_buffers(struct nf10_adapter *adapter, int rx)
{
	struct large_buffer *lbuf = get_lbuf();
	int i;
	int err = 0;

	for (i = 0; i < NR_LBUF; i++) {
		struct desc *desc = &lbuf->descs[rx][i];
		if ((err = alloc_and_map_lbuf(adapter, desc, rx)))
			break;
		netif_info(adapter, probe, adapter->netdev,
			   "%s lbuf[%d] allocated at kern_addr=%p/dma_addr=%p"
			   " pfn=%lx (size=%lu bytes)\n", rx ? "RX" : "TX", i,
			   desc->kern_addr, (void *)desc->dma_addr, 
			   page_to_pfn(desc->page), LBUF_SIZE);
	}
	

	if (unlikely(err))	/* failed to allocate all lbufs */
		for (i--; i >= 0; i--)
			unmap_and_free_lbuf(adapter, &lbuf->descs[rx][i], rx);
	else {
		if (rx)
			lbuf->rx_cons = 0;
		else
			lbuf->tx_cons = 0;
	}

	return err;
}

static void __nf10_lbuf_free_buffers(struct nf10_adapter *adapter, int rx)
{
	struct large_buffer *lbuf = get_lbuf();
	int i;

	for (i = 0; i < NR_LBUF; i++) {
		unmap_and_free_lbuf(adapter, &lbuf->descs[rx][i], rx);
		netif_info(adapter, drv, adapter->netdev,
			   "%s lbuf[%d] is freed from kern_addr=%p",
			   rx ? "RX" : "TX", i, lbuf->descs[rx][i].kern_addr);
	}
}

static void inc_rx_cons(struct nf10_adapter *adapter)
{
	struct large_buffer *lbuf = get_lbuf();

	lbuf->rx_cons++;
	if (lbuf->rx_cons == NR_LBUF)
		lbuf->rx_cons = 0;
}

static void nf10_lbuf_prepare_rx(struct nf10_adapter *adapter, unsigned long idx)
{
	struct large_buffer *lbuf = get_lbuf();

	nf10_writeq(adapter, rx_addr_off(idx), 
		    lbuf->descs[RX][idx].dma_addr);
	nf10_writel(adapter, rx_stat_off(idx), RX_READY);

	netif_dbg(adapter, rx_status, adapter->netdev,
		  "RX lbuf[%lu] is prepared to nf10\n", idx);
	if (unlikely((unsigned int)idx != lbuf->rx_cons))
		netif_warn(adapter, rx_status, adapter->netdev,
			   "prepared idx(=%lu) mismatches rx_cons=%u\n",
			   idx, lbuf->rx_cons);

	inc_rx_cons(adapter);
}

static int nf10_lbuf_deliver_skbs(struct nf10_adapter *adapter, void *kern_addr)
{
	u32 *lbuf_addr = kern_addr;	/* 32bit pointer */
	u32 nr_qwords = lbuf_addr[0];	/* first 32bit is # of qwords */
	int dword_idx, max_dword_idx = (nr_qwords << 1) + 32;
	u32 pkt_len;
	u8 bytes_remainder;
	struct net_device *netdev = adapter->netdev;
	struct sk_buff *skb;
	struct large_buffer *lbuf = get_lbuf();
	unsigned int rx_cons = lbuf->rx_cons;
	unsigned int rx_packets = 0;

	if (nr_qwords == 0 ||
	    max_dword_idx > 524288) {	/* FIXME: replace constant */
		netif_err(adapter, rx_err, netdev,
			  "rx_cons=%d's header contains invalid # of qwords=%u",
			  rx_cons, nr_qwords);
		return -1;
	}

	/* dword 1 to 31 are reserved */
	dword_idx = 32;
	do {
		dword_idx++;			/* reserved for timestamp */
		pkt_len = lbuf_addr[dword_idx++];

		/* FIXME: replace constant */
		if (pkt_len < 60 || pkt_len > 1518) {	
			netif_err(adapter, rx_err, netdev,
				  "rx_cons=%d lbuf contains invalid pkt len=%u",
				  rx_cons, pkt_len);
			goto next_pkt;
		}

		if ((skb = netdev_alloc_skb(netdev, pkt_len - 4)) == NULL) {
			netif_err(adapter, rx_err, netdev,
				  "rx_cons=%d failed to alloc skb", rx_cons);
			goto next_pkt;
		}

		memcpy(skb->data, (void *)(lbuf_addr + dword_idx), pkt_len - 4);

		skb_put(skb, pkt_len - 4);
		skb->protocol = eth_type_trans(skb, adapter->netdev);
		skb->ip_summed = CHECKSUM_NONE;

		napi_gro_receive(&adapter->napi, skb);
		
		rx_packets++;
next_pkt:
		dword_idx += pkt_len >> 2;	/* byte -> dword */
		bytes_remainder = pkt_len & 0x7;
		if (bytes_remainder >= 4)
			dword_idx++;
		else if (bytes_remainder > 0)
			dword_idx += 2;
	} while(dword_idx < max_dword_idx);

	adapter->netdev->stats.rx_packets += rx_packets;

	netif_dbg(adapter, rx_status, adapter->netdev,
		  "RX lbuf delivered nr_qwords=%u # of packets=%u/%lu\n",
		  nr_qwords, rx_packets, adapter->netdev->stats.rx_packets);

	return 0;
}

static u64 nf10_lbuf_user_init(struct nf10_adapter *adapter)
{
	struct large_buffer *lbuf = get_lbuf();

	adapter->nr_user_mmap = 0;

	/* encode the current tx_cons and rx_cons */
	return ((u64)lbuf->tx_cons << 32 | lbuf->rx_cons);
}

static unsigned long nf10_lbuf_get_pfn(struct nf10_adapter *adapter,
				       unsigned long arg)
{
	struct large_buffer *lbuf = get_lbuf();
	/* FIXME: currently, test first for rx, fetch pfn from rx first */
	int rx	= ((int)(arg / NR_LBUF) & 0x1) ^ 0x1;
	int idx	= (int)(arg % NR_LBUF);

	netif_dbg(adapter, drv, adapter->netdev,
		  "%s: rx=%d, idx=%d, arg=%lu\n", __func__, rx, idx, arg);
	return lbuf->descs[rx][idx].dma_addr >> PAGE_SHIFT;
}

static struct nf10_user_ops lbuf_user_ops = {
	.init			= nf10_lbuf_user_init,
	.get_pfn		= nf10_lbuf_get_pfn,
	.prepare_rx_buffer	= nf10_lbuf_prepare_rx,
};

/* nf10_hw_ops functions */
static int nf10_lbuf_init(struct nf10_adapter *adapter)
{
	adapter->user_ops = &lbuf_user_ops;

	return 0;
}

static void nf10_lbuf_free(struct nf10_adapter *adapter)
{
}

static int nf10_lbuf_init_buffers(struct nf10_adapter *adapter)
{
	/* TODO: TX */

	return __nf10_lbuf_init_buffers(adapter, 1);	/* RX */
}

static void nf10_lbuf_free_buffers(struct nf10_adapter *adapter)
{
	/* TODO: TX */

	__nf10_lbuf_free_buffers(adapter, 1);		/* RX */
}

static int nf10_lbuf_napi_budget(void)
{
	/* lbuf has napi budget as # of large buffers, instead of # of packets.
	 * In this regard, even budget 1 is still large, since one buffer can 
	 * contain tens of thousands of packets. Setting budget to 2 allows
	 * nf10_poll to complete polling right after handling just one lbuf,
	 * since nf10_lbuf_process_rx_irq processes one lbuf ignoring budget */
	return 2;
}

static void nf10_lbuf_prepare_rx_all(struct nf10_adapter *adapter)
{
	unsigned long i;

	for (i = 0; i < NR_LBUF; i++)
		nf10_lbuf_prepare_rx(adapter, i);
}

static void nf10_lbuf_process_rx_irq(struct nf10_adapter *adapter, 
				     int *work_done, int budget)
{
	struct large_buffer *lbuf = get_lbuf();
	struct desc *rx_descs = lbuf->descs[RX];
	unsigned int rx_cons = lbuf->rx_cons;
	dma_addr_t dma_addr;
	void *kern_addr;

	dma_addr = rx_descs[rx_cons].dma_addr;
	kern_addr = rx_descs[rx_cons].kern_addr;

	pci_dma_sync_single_for_cpu(adapter->pdev, dma_addr,
				    LBUF_SIZE, PCI_DMA_FROMDEVICE);

	/* currently, just process one large buffer, regardless of budget */
	nf10_lbuf_deliver_skbs(adapter, kern_addr);
	pci_dma_sync_single_for_device(adapter->pdev, dma_addr,
				       LBUF_SIZE, PCI_DMA_FROMDEVICE);

	nf10_lbuf_prepare_rx(adapter, (unsigned long)rx_cons);
	*work_done = 1;
}

static netdev_tx_t nf10_lbuf_start_xmit(struct nf10_adapter *adapter,
					struct sk_buff *skb,
					struct net_device *dev)
{
	/* TODO */
	return NETDEV_TX_BUSY;
}

static int nf10_lbuf_clean_tx_irq(struct nf10_adapter *adapter)
{
	/* TODO */
	return 1;
}

static struct nf10_hw_ops lbuf_hw_ops = {
	.init			= nf10_lbuf_init,
	.free			= nf10_lbuf_free,
	.init_buffers		= nf10_lbuf_init_buffers,
	.free_buffers		= nf10_lbuf_free_buffers,
	.get_napi_budget	= nf10_lbuf_napi_budget,
	.prepare_rx_buffers	= nf10_lbuf_prepare_rx_all,
	.process_rx_irq		= nf10_lbuf_process_rx_irq,
	.start_xmit		= nf10_lbuf_start_xmit,
	.clean_tx_irq		= nf10_lbuf_clean_tx_irq
};

void nf10_lbuf_set_hw_ops(struct nf10_adapter *adapter)
{
	adapter->hw_ops = &lbuf_hw_ops;
}
