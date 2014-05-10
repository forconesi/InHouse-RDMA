#include "nf10.h"

static u32 nf10_get_msglevel(struct net_device *netdev)
{
	struct nf10_adapter *adapter = netdev_priv(netdev);
	return adapter->msg_enable;
}

static void nf10_set_msglevel(struct net_device *netdev, u32 data)
{
	struct nf10_adapter *adapter = netdev_priv(netdev);
	adapter->msg_enable = data;
}

static const struct ethtool_ops nf10_ethtool_ops = {
	.get_msglevel           = nf10_get_msglevel,
	.set_msglevel           = nf10_set_msglevel,
};

void nf10_set_ethtool_ops(struct net_device *netdev)
{
	SET_ETHTOOL_OPS(netdev, &nf10_ethtool_ops);
}
