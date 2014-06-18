#include <linux/etherdevice.h>
#include "nf10.h"
#include "nf10_lbuf.h"
#include "nf10_user.h"

#ifdef CONFIG_SKBPOOL
#include "skbpool.h"
static struct skbpool_entry skb_free_list;
#endif

struct desc {
	void		*kern_addr;
	dma_addr_t	dma_addr;
	struct sk_buff	*skb;
};
#define clean_desc(desc)	\
	do { desc->kern_addr = NULL; } while(0)

struct large_buffer {
	struct desc descs[2][NR_LBUF];	/* 0=TX and 1=RX */
	unsigned int prod[2], cons[2];

	/* tx completion buffer */
	void *tx_completion_kern_addr;
	dma_addr_t tx_completion_dma_addr;
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

spinlock_t tx_lock;

/* garbage collection of tx buffer */
LIST_HEAD(pending_gc_head);
struct pending_gc_desc {
	struct list_head list;
	struct desc desc;
};
static struct kmem_cache *pending_gc_cache;
#define get_tx_last_gc_addr()	(lbuf_hw.lbuf.tx_completion_kern_addr + TX_LAST_GC_ADDR_OFFSET)

static unsigned long debug_count;	/* for debug */

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
#define LBUF_ORDER	HPAGE_PMD_ORDER

static inline void *alloc_lbuf(struct nf10_adapter *adapter, struct desc *desc)
{
#ifndef CONFIG_LBUF_COHERENT
	struct page *page;

	page = alloc_pages(GFP_TRANSHUGE, LBUF_ORDER);
	if (page)
		desc->kern_addr = page_address(page);
	else
		desc->kern_addr = NULL;
#else
	/* NOTE that pci_alloc_consistent returns allocated pages that have
	 * been zeroed, so taking longer time than normal allocation */
	desc->kern_addr = pci_alloc_consistent(adapter->pdev, LBUF_SIZE,
					       &desc->dma_addr);
#endif
	desc->skb = NULL;

	return desc->kern_addr;
}

static inline void free_lbuf(struct nf10_adapter *adapter, struct desc *desc)
{
#ifndef CONFIG_LBUF_COHERENT
	__free_pages(virt_to_page(desc->kern_addr), LBUF_ORDER);
#else
	pci_free_consistent(adapter->pdev, LBUF_SIZE,
			    desc->kern_addr, desc->dma_addr);
#endif
	/* TODO: if skb is not NULL, release it safely */
}

static void unmap_and_free_lbuf(struct nf10_adapter *adapter,
				struct desc *desc, int rx)
{
	if (unlikely(desc->kern_addr == NULL))
		return;
#ifndef CONFIG_LBUF_COHERENT	/* explicitly unmap to/from normal pages */
	pci_unmap_single(adapter->pdev, desc->dma_addr, LBUF_SIZE,
			 rx ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
#endif
	free_lbuf(adapter, desc);
	pr_debug("%s: addr=(kern=%p:dma=%p)\n", __func__, desc->kern_addr, (void *)desc->dma_addr);
}

static int alloc_and_map_lbuf(struct nf10_adapter *adapter,
			      struct desc *desc, int rx)
{
	if (alloc_lbuf(adapter, desc) == NULL)
		return -ENOMEM;

#ifndef CONFIG_LBUF_COHERENT	/* explicitly map to/from normal pages */
	desc->dma_addr = pci_map_single(adapter->pdev, desc->kern_addr,
			LBUF_SIZE, rx ? PCI_DMA_FROMDEVICE : PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(adapter->pdev, desc->dma_addr)) {
		netif_err(adapter, probe, adapter->netdev,
			  "failed to map to dma addr (kern_addr=%p)\n",
			  desc->kern_addr);
		free_lbuf(adapter, desc);
		return -EIO;
	}
#endif
	pr_debug("%s: addr=(kern=%p:dma=%p)\n", __func__, desc->kern_addr, (void *)desc->dma_addr);
	return 0;
}

static bool desc_full(struct nf10_adapter *adapter, int rx)
{
	struct large_buffer *lbuf = get_lbuf();

	/* use non-null kern_addr as an indicator that distinguishes
	 * full from empty, so make sure kern_addr sets to NULL when consumed */
	return lbuf->prod[rx] == lbuf->cons[rx] &&
	       lbuf->descs[rx][lbuf->cons[rx]].kern_addr != NULL;
}

static bool desc_empty(struct nf10_adapter *adapter, int rx)
{
	struct large_buffer *lbuf = get_lbuf();

	return lbuf->prod[rx] == lbuf->cons[rx] &&
	       lbuf->descs[rx][lbuf->cons[rx]].kern_addr == NULL;
}

#define inc_pointer(pointer)	\
	do { pointer = pointer == NR_LBUF - 1 ? 0 : pointer + 1; } while(0)
static void inc_prod(struct nf10_adapter *adapter, int rx)
{
	struct large_buffer *lbuf = get_lbuf();

	inc_pointer(lbuf->prod[rx]);
}

static void inc_cons(struct nf10_adapter *adapter, int rx)
{
	struct large_buffer *lbuf = get_lbuf();

	inc_pointer(lbuf->cons[rx]);
}

static int add_to_pending_gc_list(struct desc *desc)
{
	struct pending_gc_desc *pdesc;

	if ((pdesc = kmem_cache_alloc(pending_gc_cache, GFP_ATOMIC)) == NULL) {
		pr_err("Error: failed to alloc pdesc causing memory leak\n");
		return -ENOMEM;
	}

	pdesc->desc = *desc;	/* copy */

	/* irq must be disabled since tx irq handling can call this function */
	list_add_tail(&pdesc->list, &pending_gc_head);

	return 0;
}

static void free_pdesc(struct nf10_adapter *adapter,
		       struct pending_gc_desc *pdesc)
{
	struct desc *desc = &pdesc->desc;

	BUG_ON(!desc);

	if (desc->skb->data != desc->kern_addr)
		pr_err("Error: skb->data=%p != kern_addr=%p\n", desc->skb->data, desc->kern_addr);

	BUG_ON(!desc->kern_addr);

	/* FIXME: skb-to-desc - currently, assume one skb per desc */
	BUG_ON(desc->skb->data != desc->kern_addr);

	pr_debug("gctx: dma_addr=%p skb=%p\n", (void *)desc->dma_addr, desc->skb);

	/* FIXME: skb-to-desc - skb->len will be changed */
	pci_unmap_single(adapter->pdev, desc->dma_addr, desc->skb->len,
			 PCI_DMA_TODEVICE);

	dev_kfree_skb_any(desc->skb);

	kmem_cache_free(pending_gc_cache, pdesc);
}

static bool clean_tx_pending_gc(struct nf10_adapter *adapter, u64 last_gc_addr)
{
	struct pending_gc_desc *pdesc, *_pdesc;
	bool empty;
	
	list_for_each_entry_safe(pdesc, _pdesc, &pending_gc_head, list) {
		list_del(&pdesc->list);
		free_pdesc(adapter, pdesc);
		if (pdesc->desc.dma_addr == last_gc_addr) {
			pr_debug("[txdebug] meet gc addr and exit\n");
			break;
		}
	}
	empty = list_empty(&pending_gc_head);

	return empty;
}

static void check_tx_completion(struct nf10_adapter *adapter)
{
	struct large_buffer *lbuf = get_lbuf();
	u32 *completion = lbuf->tx_completion_kern_addr;
	unsigned int tx_cons;

	tx_cons = lbuf->cons[TX];
	while (desc_empty(adapter, TX) == false &&
	       completion[tx_cons] == TX_COMPLETION_OKAY) {
		struct desc *desc = &lbuf->descs[TX][tx_cons];

		add_to_pending_gc_list(desc);
		pr_debug("cltx[%u]: desc=%p dma_addr/kern_addr/skb=%p/%p/%p\n",
			 tx_cons, desc, (void *)desc->dma_addr, desc->kern_addr, desc->skb);

		/* clean */
		clean_desc(desc);
		completion[tx_cons] = 0;
		mb();

		/* update cons */
		inc_cons(adapter, TX);
		tx_cons = lbuf->cons[TX];
	}
	if (!debug_count || debug_count >> 13)
		pr_debug("chktx[c=%u:p=%u] - desc=%p empty=%d completion=[%x:%x] dma_addr/kern_addr/skb=%p/%p/%p\n",
		 tx_cons, lbuf->prod[TX], &lbuf->descs[TX][tx_cons], desc_empty(adapter, TX), completion[0], completion[1],
		 (void *)lbuf->descs[TX][tx_cons].dma_addr, lbuf->descs[TX][tx_cons].kern_addr, lbuf->descs[TX][tx_cons].skb);
}

static void enable_tx_intr(struct nf10_adapter *adapter)
{
	/* FIXME: replace 0xcacabeef */
	nf10_writel(adapter, TX_INTR_CTRL_ADDR, 0xcacabeef);
}

static int init_tx_completion_buffer(struct nf10_adapter *adapter)
{
	struct large_buffer *lbuf = get_lbuf();

	lbuf->tx_completion_kern_addr =
		pci_alloc_consistent(adapter->pdev, TX_COMPLETION_SIZE,
				     &lbuf->tx_completion_dma_addr);

	if (lbuf->tx_completion_kern_addr == NULL)
		return -ENOMEM;

	nf10_writeq(adapter, TX_COMPLETION_ADDR, lbuf->tx_completion_dma_addr);

	return 0;
}

static void free_tx_completion_buffer(struct nf10_adapter *adapter)
{
	struct large_buffer *lbuf = get_lbuf();

	pci_free_consistent(adapter->pdev, TX_COMPLETION_SIZE,
			    lbuf->tx_completion_kern_addr,
			    lbuf->tx_completion_dma_addr);
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
	if (unlikely((unsigned int)idx != lbuf->cons[RX]))
		netif_warn(adapter, rx_status, adapter->netdev,
			   "prepared idx(=%lu) mismatches rx_cons=%u\n",
			   idx, lbuf->cons[RX]);

	inc_cons(adapter, RX);
}

static void nf10_lbuf_prepare_rx_all(struct nf10_adapter *adapter)
{
	unsigned long i;

	for (i = 0; i < NR_LBUF; i++)
		nf10_lbuf_prepare_rx(adapter, i);
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
			   " (size=%lu bytes)\n", rx ? "RX" : "TX", i,
			   desc->kern_addr, (void *)desc->dma_addr, LBUF_SIZE);
	}
	

	if (unlikely(err))	/* failed to allocate all lbufs */
		for (i--; i >= 0; i--)
			unmap_and_free_lbuf(adapter, &lbuf->descs[rx][i], rx);
	else if (rx) 
		nf10_lbuf_prepare_rx_all(adapter);

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
	unsigned int rx_cons = lbuf->cons[RX];
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
	return ((u64)lbuf->cons[TX] << 32 | lbuf->cons[RX]);
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
	spin_lock_init(&tx_lock);
	pending_gc_cache = kmem_cache_create("pending_gc_desc",
					     sizeof(struct pending_gc_desc),
					     __alignof__(struct pending_gc_desc),
					     0, NULL);
	if (!pending_gc_cache)
		return -ENOMEM;

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
	/* 0: purge all pending descs: not empty -> bug */
	BUG_ON(!clean_tx_pending_gc(adapter, 0));
	kmem_cache_destroy(pending_gc_cache);
}

static int nf10_lbuf_init_buffers(struct nf10_adapter *adapter)
{
	struct large_buffer *lbuf = get_lbuf();
	int err = 0;

	lbuf->prod[TX] = 0;
	lbuf->prod[RX] = 0;
	lbuf->cons[TX] = 0;
	lbuf->cons[RX] = 0;

	if ((err = init_tx_completion_buffer(adapter)))
		return err;

	if ((err = __nf10_lbuf_init_buffers(adapter, RX)))
		free_tx_completion_buffer(adapter);

	return err;
}

static void nf10_lbuf_free_buffers(struct nf10_adapter *adapter)
{
	free_tx_completion_buffer(adapter);
	__nf10_lbuf_free_buffers(adapter, RX);
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

static void nf10_lbuf_process_rx_irq(struct nf10_adapter *adapter, 
				     int *work_done, int budget)
{
	struct large_buffer *lbuf = get_lbuf();
	unsigned int rx_cons = lbuf->cons[RX];
	struct desc *cur_rx_desc = &lbuf->descs[RX][rx_cons];
	struct desc rx_desc = *cur_rx_desc;	/* copy */

	return;		/* FIXME-delete: for tx debug */

	alloc_and_map_lbuf(adapter, cur_rx_desc, RX);
	nf10_lbuf_prepare_rx(adapter, (unsigned long)rx_cons);

#ifndef CONFIG_LBUF_COHERENT
	pci_dma_sync_single_for_cpu(adapter->pdev, rx_desc.dma_addr,
				    LBUF_SIZE, PCI_DMA_FROMDEVICE);
#endif
	/* if a user process can handle it, pass it up and return */
	if (nf10_user_rx_callback(adapter))
		return;

#ifdef CONFIG_XEN_NF_BACKEND
	/* if failed to deliver to frontend, fallback with host processing.
	 * currently, domid is set as an arbitrary number (1), since we don't
	 * have packet classification in hardware right now */
	if (xenvif_rx_action(1 /* FIXME */, rx_desc.kern_addr, LBUF_SIZE))
#endif
	/* currently, just process one large buffer, regardless of budget */
	nf10_lbuf_deliver_skbs(adapter, rx_desc.kern_addr);
	unmap_and_free_lbuf(adapter, &rx_desc, RX);
	*work_done = 1;
}

static netdev_tx_t nf10_lbuf_start_xmit(struct nf10_adapter *adapter,
					struct sk_buff *skb,
					struct net_device *dev)
{
	struct large_buffer *lbuf = get_lbuf();
	struct desc *desc;
	unsigned int tx_prod;
	u32 nr_qwords;
	unsigned long flags;

	spin_lock_irqsave(&tx_lock, flags);

	check_tx_completion(adapter);

	if (desc_full(adapter, TX)) {
		if (++debug_count >> 13) {
			pr_warn("WARN: tx desc is full! (%lu)\n", debug_count);
			debug_count = 0;
		}
#if 0	/* TODO */
		netif_stop_queue(dev);
#endif
		spin_unlock_irqrestore(&tx_lock, flags);
		return NETDEV_TX_BUSY;
	}
	debug_count = 0;

	tx_prod = lbuf->prod[TX];
	desc = &lbuf->descs[TX][tx_prod];

	////pr_debug("tx[%u]: len=%u, head=%p, data=%p\n", tx_prod, skb->len, skb->head, skb->data);

	/* TODO: check availability */

	/* TODO: check if enough headroom exists, if not copy...
	 * FIXME: replace 8 with macro */
	skb_push(skb, 8);
	((u32 *)skb->data)[0] = 0;
	((u32 *)skb->data)[1] = skb->len - 8;

	/* FIXME: do we need padding something at tailroom? */

	nr_qwords = ALIGN(skb->len, 8) >> 3;

	/* FIXME: skb-to-desc */
	desc->dma_addr = pci_map_single(adapter->pdev, skb->data, skb->len,
					PCI_DMA_TODEVICE);
	if (pci_dma_mapping_error(adapter->pdev, desc->dma_addr)) {
		netif_err(adapter, probe, adapter->netdev,
			  "failed to map to dma addr (kern_addr=%p)\n",
			  desc->kern_addr);
		spin_unlock_irqrestore(&tx_lock, flags);
		return NETDEV_TX_BUSY;
	}
	desc->kern_addr = skb->data;
	desc->skb = skb;
	/* TODO: check dma map error */

	pr_debug("\trqtx[%u]: desc=%p len=%u, head=%p, dma_addr/kern_addr/skb=%p/%p/%p, nr_qwords=%u, addr=0x%x, stat=0x%x\n",
		 tx_prod, desc, skb->len, skb->head, (void *)desc->dma_addr, desc->kern_addr, desc->skb,
		 nr_qwords, tx_addr_off(tx_prod), tx_stat_off(tx_prod));

	nf10_writeq(adapter, tx_addr_off(tx_prod), desc->dma_addr);
	nf10_writel(adapter, tx_stat_off(tx_prod), nr_qwords);

	inc_prod(adapter, TX);

	spin_unlock_irqrestore(&tx_lock, flags);

	return NETDEV_TX_OK;
}

static int nf10_lbuf_clean_tx_irq(struct nf10_adapter *adapter)
{
	u64 *tx_last_gc_addr_ptr = get_tx_last_gc_addr();
	dma_addr_t tx_last_gc_addr;
	int complete;
	unsigned long flags;

	return 1;

	/* TODO: optimization possible in case where one-by-one tx/completion,
	 * we can avoid add and delete to-be-cleaned desc to/from gc list */
again:
	spin_lock_irqsave(&tx_lock, flags);
	check_tx_completion(adapter);

	tx_last_gc_addr = *tx_last_gc_addr_ptr;
	pr_debug("tx-irq: gc_addr=%p\n", (void *)tx_last_gc_addr);

	/* no gc buffer updated */
	if (tx_last_gc_addr == 0) {
		spin_unlock_irqrestore(&tx_lock, flags);
		return 1;	/* clean complete */
	}

	complete = clean_tx_pending_gc(adapter, tx_last_gc_addr);

	spin_unlock_irqrestore(&tx_lock, flags);

	if (tx_last_gc_addr != *tx_last_gc_addr_ptr) {
		pr_debug("\ttry to clean again at 1st pass\n");
		goto again;
	}

	if (cmpxchg64(tx_last_gc_addr_ptr, tx_last_gc_addr, 0) != tx_last_gc_addr) {
		pr_debug("\ttry to clean again at 2nd pass\n");
		goto again;
	}

	pr_debug("\tclean complete=%d\n", complete);

	return complete;
}

static unsigned long nf10_lbuf_ctrl_irq(struct nf10_adapter *adapter,
					unsigned long cmd)
{
	if (cmd == IRQ_CTRL_TX_ENABLE) {
		pr_debug("tx irq enabled\n");
		enable_tx_intr(adapter);
	}
	return 0;
}

static struct nf10_hw_ops lbuf_hw_ops = {
	.init			= nf10_lbuf_init,
	.free			= nf10_lbuf_free,
	.init_buffers		= nf10_lbuf_init_buffers,
	.free_buffers		= nf10_lbuf_free_buffers,
	.get_napi_budget	= nf10_lbuf_napi_budget,
	.process_rx_irq		= nf10_lbuf_process_rx_irq,
	.start_xmit		= nf10_lbuf_start_xmit,
	.clean_tx_irq		= nf10_lbuf_clean_tx_irq,
	.ctrl_irq		= nf10_lbuf_ctrl_irq
};

void nf10_lbuf_set_hw_ops(struct nf10_adapter *adapter)
{
	adapter->hw_ops = &lbuf_hw_ops;
}
