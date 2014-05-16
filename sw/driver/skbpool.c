#include "skbpool.h"

static struct kmem_cache *skbpool_cache;
static struct workqueue_struct *skbpool_wq;
static struct work_struct skbpool_work;

static struct skbpool_head skbpool_list;
static atomic_t nr_skbpool_list;

/* paramters set at init time */
static struct net_device *skb_netdev;
static unsigned int skb_data_len;
static unsigned long min_skbpool_list;
static unsigned int nr_batch_alloc;

static struct skbpool_entry *skbpool_entry_alloc(void)
{
	struct skbpool_entry *entry =
		kmem_cache_alloc(skbpool_cache, GFP_ATOMIC);

	if (entry) {
		entry->skb = netdev_alloc_skb(skb_netdev, skb_data_len);
		if (entry->skb == NULL) {
			skbpool_free(entry);
			entry = NULL;
		}
	}

	return entry;
}

static void skbpool_purge(struct skbpool_head *head)
{
	struct skbpool_entry *skb_entry, *p, *n;

	skb_entry = skbpool_del_all(head);
	if (skb_entry == NULL)
		return;

	skbpool_for_each_entry_safe(p, n, skb_entry) {
		if (likely(p->skb))
			kfree_skb(p->skb);
		skbpool_free(p);
	}
}

static void skbpool_worker(struct work_struct *work)
{
	struct skbpool_entry *head, *tail;	/* local batch */
	struct skbpool_entry *entry;
	int nr_alloc;

	while(atomic_read(&nr_skbpool_list) < min_skbpool_list) {
		head = tail = NULL;
		for (nr_alloc = 0; nr_alloc < nr_batch_alloc; nr_alloc++) {
			entry = skbpool_entry_alloc();

			if (unlikely(entry == NULL))
				break;

			if (head)
				entry->node.next = &head->node;
			else	/* first added entry is at tail */
				tail = entry;
			head = entry;
		}

		/* give up when memory alloc failed */
		if (unlikely(head == NULL))
			return;

		skbpool_add_batch(head, tail, &skbpool_list);
		atomic_add(nr_alloc, &nr_skbpool_list);

#if 0
		pr_debug("skbpool: skb filled %d/%d\n",
			 nr_alloc, atomic_read(&nr_skbpool_list));
#endif
	}
}

struct skbpool_entry *skbpool_alloc(void)
{
	struct skbpool_entry *entry;

	if (atomic_read(&nr_skbpool_list) < min_skbpool_list)
		queue_work_on(num_online_cpus() - 1, skbpool_wq, &skbpool_work);

	entry = skbpool_del(&skbpool_list);

	if (unlikely(entry == NULL)) {	/* fallback */
		entry = skbpool_entry_alloc();
		//pr_warn("skbpool: pool alloc lags behind request rate\n");
	}
	else {
		atomic_dec(&nr_skbpool_list);	/* eventually consistent */
#if 0
		pr_debug("skbpool: alloc okay (%d)\n",
			 atomic_read(&nr_skbpool_list));
#endif
	}

	return entry;
}

void skbpool_free(struct skbpool_entry *entry)
{
	kmem_cache_free(skbpool_cache, entry);
}

int skbpool_init(struct net_device *netdev, unsigned int length,
		 unsigned long min_list, unsigned int nr_alloc)
{
	skbpool_cache = kmem_cache_create("skbpool_entry",
					  sizeof(struct skbpool_entry),
					  __alignof__(struct skbpool_entry),
					  0, NULL);
	if (!skbpool_cache)
		return -ENOMEM;

	skbpool_head_init(&skbpool_list);

	INIT_WORK(&skbpool_work, skbpool_worker);
	skbpool_wq = alloc_ordered_workqueue("skbpool", 0);
	if (skbpool_wq == NULL) {
		pr_err("failed to alloc skbpool workqueue\n");
		return -ENOMEM;
	}

	skb_netdev = netdev;
	skb_data_len = length;
	min_skbpool_list = min_list;
	nr_batch_alloc = nr_alloc;

	pr_info("skbpool: initialized w/ len=%u min_list=%lu nr_alloc=%u\n",
		skb_data_len, min_skbpool_list, nr_batch_alloc);

	if (atomic_read(&nr_skbpool_list) < min_list)
		queue_work(skbpool_wq, &skbpool_work);

	return 0;
}

void skbpool_destroy(struct skbpool_head *head)
{
	destroy_workqueue(skbpool_wq);
	skbpool_purge(head);
	skbpool_purge(&skbpool_list);
	atomic_set(&nr_skbpool_list, 0);
	kmem_cache_destroy(skbpool_cache);
}
