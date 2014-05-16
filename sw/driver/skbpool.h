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

static inline void skbpool_head_init(struct skbpool_head *head)
{
	init_llist_head(&head->head);
}

static inline bool skbpool_add(struct skbpool_entry *entry,
			       struct skbpool_head  *head)
{
	return llist_add(&entry->node, &head->head);
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
extern struct skbpool_entry *skbpool_alloc(struct net_device *netdev,
					   unsigned int length);
extern void skbpool_free(struct skbpool_entry *entry);
extern int skbpool_init(void);
extern void skbpool_destroy(void);
