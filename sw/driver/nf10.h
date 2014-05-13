#ifndef _NF10_H
#define _NF10_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include "nf10_lbuf.h"

#define NF10_VENDOR_ID	0x10ee
#define NF10_DEVICE_ID	0x4245

struct nf10_adapter {
	struct napi_struct napi;
	struct net_device *netdev;
	struct pci_dev *pdev;

	u8 __iomem *bar0;
	u8 __iomem *bar2;

	union {
		struct large_buffer lbuf;
	};

	u16 msg_enable;

	/* for phy chip */
	atomic_t mdio_access_rdy;

	/* direct user access (kernel bypass) */
	struct nf10_user_ops *user_ops;
	struct cdev cdev;
	unsigned int nr_user_mmap;
	wait_queue_head_t wq_user_intr;
};

struct nf10_hw_ops {
	int		(*init_buffers)(struct pci_dev *pdev);
	void		(*free_buffers)(struct pci_dev *pdev);
	int		(*get_napi_budget)(void);
	void		(*prepare_rx_buffers)(struct pci_dev *pdev);
	void		(*process_rx_irq)(struct pci_dev *pdev, 
					  int *work_done, int budget);
	netdev_tx_t     (*start_xmit)(struct sk_buff *skb, 
				      struct net_device *dev);
	int		(*clean_tx_irq)(struct pci_dev *pdev);
};

struct nf10_user_ops {
	unsigned long	(*get_pfn)(struct nf10_adapter *adapter, unsigned long arg);
	void		(*prepare_rx_buffer)(struct nf10_adapter *adapter,
					     unsigned long arg);
};

extern char nf10_driver_name[];

static inline void nf10_writel(struct nf10_adapter *adapter, int off, u32 val)
{
	writel(val, (u32 *)adapter->bar2 + off);
}

static inline void nf10_writeq(struct nf10_adapter *adapter, int off, u64 val)
{
	writeq(val, (u64 *)adapter->bar2 + off);
}

extern void nf10_set_ethtool_ops(struct net_device *netdev);

#endif
