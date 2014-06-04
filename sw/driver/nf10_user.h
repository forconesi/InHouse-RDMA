#ifndef _NF10_FOPS_H
#define _NF10_FOPS_H

#define NF10_DRV_NAME			"nf10"
#define NF10_IOCTL_CMD_INIT		(SIOCDEVPRIVATE+0)
#define NF10_IOCTL_CMD_PREPARE_RX	(SIOCDEVPRIVATE+1)
#define NF10_IOCTL_CMD_WAIT_INTR	(SIOCDEVPRIVATE+2)

#ifdef __KERNEL__
#include "nf10.h"
extern int nf10_init_fops(struct nf10_adapter *adapter);
extern int nf10_remove_fops(struct nf10_adapter *adapter);
extern bool nf10_user_rx_callback(struct nf10_adapter *adapter);
#endif

#endif
