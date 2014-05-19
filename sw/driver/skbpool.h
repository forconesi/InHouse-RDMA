#include <linux/llist.h>
#include <linux/netdevice.h>

struct skbpool_entry {
	struct sk_buff *skb;
	struct llist_node node;
};

struct skbpool_head {
	struct llist_head head;
};

#define skbpool_for_each_entry(pos, entry)	\
	llist_for_each_entry(pos, &entry->node, node)

#define skbpool_for_each_entry_safe(pos, n, entry)	\
	llist_for_each_entry_safe(pos, n, &entry->node, node)

#define skbpool_next_entry(entry)	\
	llist_entry(entry->node.next, struct skbpool_entry, node)

static inline void skbpool_head_init(struct skbpool_head *head)
{
	init_llist_head(&head->head);
}

static inline bool skbpool_add(struct skbpool_entry *entry,
			       struct skbpool_head  *head)
{
	return llist_add(&entry->node, &head->head);
}

static inline bool skbpool_add_batch(struct skbpool_entry *entry_first,
				     struct skbpool_entry *entry_last,
				     struct skbpool_head  *head)
{
	return llist_add_batch(&entry_first->node,
			       &entry_last->node,
			       &head->head);
}

static inline struct skbpool_entry *skbpool_del(struct skbpool_head *head)
{
	struct llist_node *node = llist_del_first(&head->head);

	if (!node)
		return NULL;

	return llist_entry(node, struct skbpool_entry, node);
}

static inline struct skbpool_entry *skbpool_del_all(struct skbpool_head *head)
{
	struct llist_node *node = llist_del_all(&head->head);

	if (!node)
		return NULL;

	return llist_entry(node, struct skbpool_entry, node);
}

extern struct skbpool_entry *skbpool_alloc_single(void);
extern struct skbpool_entry *skbpool_alloc(struct skbpool_entry *entry);
extern void skbpool_free(struct skbpool_entry *entry);
extern void skbpool_purge(struct skbpool_entry *entry);
extern void skbpool_purge_head(struct skbpool_head *head);
extern int skbpool_init(struct net_device *netdev, unsigned int length,
			unsigned long min_list, unsigned int nr_alloc);
extern void skbpool_destroy(void);
