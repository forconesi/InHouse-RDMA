#ifndef _NF10_FOPS_H
#define _NF10_FOPS_H

#define NF10_DRV_NAME			"nf10"
#define NF10_IOCTL_CMD_INIT		(SIOCDEVPRIVATE+0)
#define NF10_IOCTL_CMD_PREPARE_RX	(SIOCDEVPRIVATE+1)
#define NF10_IOCTL_CMD_WAIT_INTR	(SIOCDEVPRIVATE+2)

#ifdef __KERNEL__
#include <linux/pci.h>
extern int nf10_init_fops(struct pci_dev *pdev);
extern int nf10_remove_fops(struct pci_dev *pdev);
#endif

#endif
