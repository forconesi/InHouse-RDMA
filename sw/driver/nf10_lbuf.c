#include <linux/etherdevice.h>
#include "nf10.h"
#include "nf10_lbuf.h"

#ifdef CONFIG_SKBPOOL
#include "skbpool.h"
static struct skbpool_entry skb_free_list;
#endif

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
	struct nf10_adapter *adapter;
	struct large_buffer lbuf;

#ifdef CONFIG_SKBPOOL
	struct skbpool_head rxq;
#endif
	struct workqueue_struct *rx_wq;
	struct work_struct rx_work;
} lbuf_hw;
#define get_lbuf()	(&lbuf_hw.lbuf)
#define get_rx_work()	(&lbuf_hw.rx_work)
#ifdef CONFIG_SKBPOOL
#define get_rxq()	(&lbuf_hw.rxq)
#endif

/* profiling memcpy performance */
#ifdef CONFIG_PROFILE
/* WARN: note that it does not do bound check for performance */
#define DEFINE_TIMESTAMP(n)	u64	_t1, _t2, _total[n] = {0}
#define START_TIMESTAMP(i)	rdtscll(_t1)
#define STOP_TIMESTAMP(i)	\
	do {	\
		rdtscll(_t2);	\
		_total[i] += (_t2 - _t1);	\
	} while(0)
#define ELAPSED_CYCLES(i)	(_total[i])
#else
#define DEFINE_TIMESTAMP(n)
#define START_TIMESTAMP(i)
#define STOP_TIMESTAMP(i)
#define ELAPSED_CYCLES(i)	(0ULL)
#endif

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
#ifdef CONFIG_LBUF_COHERENT
	pci_free_consistent(adapter->pdev, LBUF_SIZE,
			    desc->kern_addr, desc->dma_addr);
#else
	pci_unmap_single(adapter->pdev, desc->dma_addr, LBUF_SIZE,
			rx ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
	free_lbuf(desc->page);
#endif
}

static int alloc_and_map_lbuf(struct nf10_adapter *adapter,
			      struct desc *desc, int rx)
{
retry:
#ifdef CONFIG_LBUF_COHERENT
	desc->kern_addr = pci_alloc_consistent(adapter->pdev, LBUF_SIZE,
					       &desc->dma_addr);
	if (desc->kern_addr == NULL)
		return -ENOMEM;
	desc->page = virt_to_page(desc->kern_addr);
#else
	if ((desc->page = alloc_lbuf()) == NULL)
		return -ENOMEM;

	desc->kern_addr = page_address(desc->page);
	desc->dma_addr = pci_map_single(adapter->pdev, desc->kern_addr,
			LBUF_SIZE, rx ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(adapter->pdev, desc->dma_addr)) {
		free_lbuf(desc->page);
		netif_err(adapter, probe, adapter->netdev,
			  "failed to map to dma addr (kern_addr=%p)\n",
			  desc->kern_addr);
		return -EIO;
	}
#endif

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
	dma_addr_t dma_addr = lbuf->descs[RX][idx].dma_addr;

#ifndef CONFIG_LBUF_COHERENT
	/* this function can be called from user thread via ioctl,
	 * so this mapping should be done safely in that case */
	pci_dma_sync_single_for_device(adapter->pdev, dma_addr,
				       LBUF_SIZE, PCI_DMA_FROMDEVICE);
#endif
	nf10_writeq(adapter, rx_addr_off(idx), dma_addr);
	nf10_writel(adapter, rx_stat_off(idx), RX_READY);

	netif_dbg(adapter, rx_status, adapter->netdev,
		  "RX lbuf[%lu] is prepared to nf10\n", idx);
	if (unlikely((unsigned int)idx != lbuf->rx_cons))
		netif_warn(adapter, rx_status, adapter->netdev,
			   "prepared idx(=%lu) mismatches rx_cons=%u\n",
			   idx, lbuf->rx_cons);

	inc_rx_cons(adapter);
}

#ifdef CONFIG_SKBPOOL
static void nf10_lbuf_rx_worker(struct work_struct *work)
{
	struct nf10_adapter *adapter = lbuf_hw.adapter;
	struct skbpool_entry *skb_entry, *p, *n;
	
	skb_entry = skbpool_del_all(get_rxq());

	if (skb_entry == NULL)
		return;

	skbpool_for_each_entry(p, skb_entry)
		napi_gro_receive(&adapter->napi, p->skb);

	skbpool_for_each_entry_safe(p, n, skb_entry)
		skbpool_free(p);
}
#endif

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
	unsigned int data_len;
#ifdef CONFIG_SKBPOOL
	struct skbpool_entry *skb_entry = &skb_free_list;
	struct skbpool_entry *skb_entry_first = NULL;	/* local header */
	struct skbpool_entry *skb_entry_last = NULL;
#endif
	DEFINE_TIMESTAMP(3);

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
#ifdef CONFIG_SKBPOOL
		skbpool_prefetch_next(skb_entry);
#endif

		dword_idx++;			/* reserved for timestamp */
		pkt_len = lbuf_addr[dword_idx++];

		/* FIXME: replace constant */
		if (unlikely(pkt_len < 60 || pkt_len > 1518)) {	
			netif_err(adapter, rx_err, netdev,
				  "rx_cons=%d lbuf contains invalid pkt len=%u",
				  rx_cons, pkt_len);
			goto next_pkt;
		}
		data_len = pkt_len - 4;

		START_TIMESTAMP(0);
#ifdef CONFIG_SKBPOOL
		if (unlikely((skb_entry = skbpool_alloc(skb_entry)) == NULL)) {
#else
		if ((skb = netdev_alloc_skb(netdev, data_len)) == NULL) {
#endif
			netif_err(adapter, rx_err, netdev,
				  "rx_cons=%d failed to alloc skb", rx_cons);
			goto next_pkt;
		}
		STOP_TIMESTAMP(0);
#ifdef CONFIG_SKBPOOL
		if (unlikely(skb_entry_first == NULL))
			skb_entry_first = skb_entry;
		skb_entry_last = skb_entry;
		skb = skb_entry->skb;
#endif

		START_TIMESTAMP(1);
		skb_copy_to_linear_data(skb, (void *)(lbuf_addr + dword_idx),
					data_len);	/* memcpy */
		STOP_TIMESTAMP(1);

		skb_put(skb, data_len);
		skb->protocol = eth_type_trans(skb, adapter->netdev);
		skb->ip_summed = CHECKSUM_NONE;

		START_TIMESTAMP(2);
#ifndef CONFIG_SKBPOOL
		napi_gro_receive(&adapter->napi, skb);
#endif
		STOP_TIMESTAMP(2);
		rx_packets++;
next_pkt:
		dword_idx += pkt_len >> 2;	/* byte -> dword */
		bytes_remainder = pkt_len & 0x7;
		if (bytes_remainder >= 4)
			dword_idx++;
		else if (bytes_remainder > 0)
			dword_idx += 2;
	} while(dword_idx < max_dword_idx);

#ifdef CONFIG_SKBPOOL
	if (likely(skb_entry_last)) {
		skb_free_list = *skb_entry_last;
		skbpool_add_batch(skb_entry_first, skb_entry_last, get_rxq());
		queue_work(lbuf_hw.rx_wq, get_rx_work());
	}
#endif
	adapter->netdev->stats.rx_packets += rx_packets;

	netif_dbg(adapter, rx_status, adapter->netdev,
		  "RX lbuf delivered nr_qwords=%u rx_packets=%u/%lu" 
		  " alloc=%llu memcpy=%llu skbpass=%llu\n",
		  nr_qwords, rx_packets, adapter->netdev->stats.rx_packets,
		  ELAPSED_CYCLES(0), ELAPSED_CYCLES(1), ELAPSED_CYCLES(2));

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
#ifdef CONFIG_SKBPOOL
	int err;

	INIT_WORK(get_rx_work(), nf10_lbuf_rx_worker);
	lbuf_hw.rx_wq = alloc_workqueue("lbuf_rx", WQ_MEM_RECLAIM, 0);
	if (lbuf_hw.rx_wq == NULL) {
		netif_err(adapter, rx_err, adapter->netdev,
			  "failed to alloc lbuf rx workqueue\n");
		return -ENOMEM;
	}

	skbpool_head_init(get_rxq());
	/* FIXME: replace constants */
	if ((err = skbpool_init(adapter->netdev, 1518, 30000, 3000))) {
		netif_err(adapter, rx_err, adapter->netdev,
			  "failed to init skbpool\n");
		return err;
	}
#endif
	lbuf_hw.adapter = adapter;
	adapter->user_ops = &lbuf_user_ops;

	return 0;
}

static void nf10_lbuf_free(struct nf10_adapter *adapter)
{
#ifdef CONFIG_SKBPOOL
	skbpool_purge(&skb_free_list);
	skbpool_purge_head(get_rxq());
	skbpool_destroy();
	destroy_workqueue(lbuf_hw.rx_wq);
#endif
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

#ifndef CONFIG_LBUF_COHERENT
	pci_dma_sync_single_for_cpu(adapter->pdev, dma_addr,
				    LBUF_SIZE, PCI_DMA_FROMDEVICE);
#endif
	/* if direct user access mode is enabled, just wake up
	 * a waiting user thread. FIXME: replace with API */
	if (adapter->nr_user_mmap > 0) { 
		if (likely(waitqueue_active(&adapter->wq_user_intr)))
			wake_up(&adapter->wq_user_intr);
		/* in case a user thread has mapped rx buffers, but
		 * not waiting for an interrupt, just skip it while granting
		 * an opportunity for the thread to poll buffers later */
		return;
	}
#ifdef CONFIG_XEN_NF_BACKEND
	/* FIXME: test code */
	if (1) {
		unsigned long size = LBUF_SIZE;
		unsigned long unit = size >> 3;	/* divided by 8, so 256K at a time */
		while (size > 0 &&
		       xenvif_rx_action(1, kern_addr, unit) == 0) {
			kern_addr += unit;
			size -= unit;
		}
		*work_done = 1;
		return;
	}
	else
#endif
	/* currently, just process one large buffer, regardless of budget */
	nf10_lbuf_deliver_skbs(adapter, kern_addr);

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
