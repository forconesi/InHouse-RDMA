#include "skbpool.h"

static struct kmem_cache *skbpool_cache;

struct skbpool_entry *skbpool_alloc(struct net_device *netdev,
				    unsigned int length)
{
	struct skbpool_entry *entry =
		kmem_cache_alloc(skbpool_cache, GFP_ATOMIC);

	if (entry) {
		entry->skb = netdev_alloc_skb(netdev, length);
		if (entry->skb == NULL) {
			skbpool_free(entry);
			entry = NULL;
		}
	}

	return entry;
}

void skbpool_free(struct skbpool_entry *entry)
{
	kmem_cache_free(skbpool_cache, entry);
}

int skbpool_init(void)
{
	skbpool_cache = kmem_cache_create("skbpool_entry",
					  sizeof(struct skbpool_entry),
					  __alignof__(struct skbpool_entry),
					  0, NULL);
	if (!skbpool_cache)
		return -ENOMEM;

	return 0;
}

void skbpool_destroy(void)
{
	kmem_cache_destroy(skbpool_cache);
}
