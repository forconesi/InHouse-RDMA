#ifndef _NF10_H
#define _NF10_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include "nf10_lbuf.h"

#define NF10_VENDOR_ID	0x10ee
#define NF10_DEVICE_ID	0x4245

struct nf10_adapter {
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
};

extern char nf10_driver_name[];

#endif
