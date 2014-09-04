#ifndef _NF10_LBUF_H
#define _NF10_LBUF_H

/* NR_LBUF is dependent on lbuf DMA engine */
#define NR_LBUF		2

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/list.h>
#include "nf10.h"

/* offset to bar2 address of the card */
#define RX_LBUF_ADDR_BASE	0x40
#define RX_LBUF_STAT_BASE	0x60
#define RX_READY		0x1
#define TX_LBUF_ADDR_BASE	0x80
#define TX_LBUF_STAT_BASE	0xA0
#define TX_COMPLETION_ADDR	0xB0
#define TX_INTR_CTRL_ADDR	0xB8
#define TX_COMPLETION_SIZE	((NR_LBUF << 2) + 8)	/* DWORD for each desc + QWORD (last gc addr) */
#define TX_LAST_GC_ADDR_OFFSET	(NR_LBUF << 2)		/* last gc addr following completion buffers for all descs */
#define TX_COMPLETION_OKAY	0xcacabeef

#define rx_addr_off(i)	(RX_LBUF_ADDR_BASE + (i << 3))
#define rx_stat_off(i)	(RX_LBUF_STAT_BASE + (i << 2))
#define tx_addr_off(i)	(TX_LBUF_ADDR_BASE + (i << 3))
#define tx_stat_off(i)	(TX_LBUF_STAT_BASE + (i << 2))

#define TX	0
#define RX	1

struct nf10_adapter;
extern void nf10_lbuf_set_hw_ops(struct nf10_adapter *adapter);

struct desc {
	void			*kern_addr;
	dma_addr_t		dma_addr;
	struct sk_buff		*skb;
	u32			size;
	u32			offset;
	struct list_head	list;
};
#define clean_desc(desc)	\
	do { desc->kern_addr = NULL; } while(0)

struct lbuf_head {
	struct list_head head;
	spinlock_t lock;
};

static inline void lbuf_head_init(struct lbuf_head *head)
{
	INIT_LIST_HEAD(&head->head);
	spin_lock_init(&head->lock);
}

static inline int lbuf_queue_empty(struct lbuf_head *head)
{
	return list_empty(&head->head);
}

static inline void __lbuf_queue_tail(struct lbuf_head *head, struct desc *desc)
{
	list_add_tail(&desc->list, &head->head);
}

static inline void __lbuf_queue_head(struct lbuf_head *head, struct desc *desc)
{
	list_add(&desc->list, &head->head);
}

static inline struct desc *__lbuf_dequeue(struct lbuf_head *head)
{
	struct desc *desc = NULL;

	if (!list_empty(&head->head)) {
		desc = list_first_entry(&head->head, struct desc, list);
		list_del(&desc->list);
	}
	return desc;
}
extern void lbuf_queue_tail(struct lbuf_head *head, struct desc *desc);
extern void lbuf_queue_head(struct lbuf_head *head, struct desc *desc);
extern struct desc *lbuf_dequeue(struct lbuf_head *head);
extern void release_lbuf(struct nf10_adapter *adapter, struct desc *desc);
#endif

#endif
