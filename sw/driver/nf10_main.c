#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>

#include "nf10.h"
#include "nf10_user.h"

u64 nf10_test_dev_addr = 0x000f530dd165;

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV|NETIF_MSG_PROBE|NETIF_MSG_LINK|NETIF_MSG_IFDOWN|NETIF_MSG_IFUP|NETIF_MSG_RX_ERR)
static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level");

static bool reset = false;
module_param(reset, bool, 0644);
MODULE_PARM_DESC(reset, "PCIe reset sent");

/* DMA engine-dependent functions */
enum {
	DMA_LARGE_BUFFER = 0,
};
static int dma_mode = DMA_LARGE_BUFFER;
module_param(dma_mode, int, 0);
MODULE_PARM_DESC(dma_mode, "nf10 DMA version (0: large buffer)");

static int nf10_init(struct nf10_adapter *adapter)
{
	if (dma_mode == DMA_LARGE_BUFFER)
		nf10_lbuf_set_hw_ops(adapter);
	else
		return -EINVAL;

	if (unlikely(adapter->hw_ops == NULL))
		return -EINVAL;

	return adapter->hw_ops->init(adapter);
}

static void nf10_free(struct nf10_adapter *adapter)
{
	adapter->hw_ops->free(adapter);
}

static int nf10_init_buffers(struct nf10_adapter *adapter)
{
	return adapter->hw_ops->init_buffers(adapter);
}

static void nf10_free_buffers(struct nf10_adapter *adapter)
{
	adapter->hw_ops->free_buffers(adapter);
}

static int nf10_napi_budget(struct nf10_adapter *adapter)
{
	return adapter->hw_ops->get_napi_budget();
}

void nf10_process_rx_irq(struct nf10_adapter *adapter, int *work_done, int budget)
{
	adapter->hw_ops->process_rx_irq(adapter, work_done, budget);
}

static netdev_tx_t nf10_start_xmit(struct sk_buff *skb,
				   struct net_device *netdev)
{
	struct nf10_adapter *adapter = netdev_priv(netdev);

	return adapter->hw_ops->start_xmit(adapter, skb, netdev);
}

static int nf10_clean_tx_irq(struct nf10_adapter *adapter)
{
	return adapter->hw_ops->clean_tx_irq(adapter);
}

static void nf10_enable_tx_irq(struct nf10_adapter *adapter)
{
	adapter->hw_ops->ctrl_irq(adapter, IRQ_CTRL_TX_ENABLE);
}

static int nf10_up(struct net_device *netdev)
{
	struct nf10_adapter *adapter = netdev_priv(netdev);
	int err;

	if (reset == false) {
		if ((err = pci_reset_bus(adapter->pdev->bus))) {
			netif_err(adapter, ifup, netdev,
				  "failed to reset bus (err=%d)\n", err);
			return err;
		}
		reset = true;
		netif_info(adapter, ifup, netdev, "PCIe bus is reset\n");
	}

	if ((err = nf10_init_buffers(adapter))) {
		netif_err(adapter, ifup, netdev,
			  "failed to initialize packet buffers: err=%d\n", err);
		return err;
	}

	nf10_enable_tx_irq(adapter);
	netif_start_queue(netdev);

	/* FIXME: to test, put the reset of some stats */
	netdev->stats.rx_packets = 0;

	netif_info(adapter, ifup, netdev, "up\n");
	return 0;
}

static int nf10_down(struct net_device *netdev)
{
	struct nf10_adapter *adapter = netdev_priv(netdev);

	nf10_free_buffers(adapter);
	netif_stop_queue(netdev);
	/* TODO */

	netif_info(adapter, ifdown, netdev, "down\n");
	return 0;
}

static const struct net_device_ops nf10_netdev_ops = {
	.ndo_open		= nf10_up,
	.ndo_stop		= nf10_down,
	.ndo_start_xmit		= nf10_start_xmit
};

irqreturn_t nf10_interrupt_handler(int irq, void *data)
{
	struct pci_dev *pdev = data;
	struct nf10_adapter *adapter = pci_get_drvdata(pdev);

	netif_dbg(adapter, intr, adapter->netdev, "IRQ delivered\n");

	/* TODO: IRQ disable */
	
	napi_schedule(&adapter->napi);

	return IRQ_HANDLED;
}

extern irqreturn_t mdio_access_interrupt_handler(int irq, void *dev_id);
extern int configure_ael2005_phy_chips(struct nf10_adapter *adapter);

static int nf10_init_phy(struct pci_dev *pdev)
{
	/* AEL2005 MDIO configuration */
	int err = 0;
	struct nf10_adapter *adapter = pci_get_drvdata(pdev);
	if ((err = request_irq(pdev->irq, mdio_access_interrupt_handler,
					0, NF10_DRV_NAME, pdev)))
		return err;
	err = configure_ael2005_phy_chips(adapter);
	free_irq(pdev->irq, pdev);

	return err;
}

int nf10_poll(struct napi_struct *napi, int budget)
{       
	struct nf10_adapter *adapter = 
		container_of(napi, struct nf10_adapter, napi);
	int tx_clean_complete, work_done = 0;

	tx_clean_complete = nf10_clean_tx_irq(adapter);

	nf10_process_rx_irq(adapter, &work_done, budget);

	if (!tx_clean_complete)
		work_done = budget;

	if (work_done < budget) {
		napi_complete(napi);

		/* TODO: enable IRQ */
	}

	return work_done;
}

static int nf10_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct nf10_adapter *adapter;
	struct net_device *netdev;
	int err;

	if ((err = pci_enable_device(pdev)))
		return err;

	if ((err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) ||
	    (err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64)))) {
		pr_err("DMA configuration failed to set 64bit mask\n");
		goto err_dma;
	}

	if ((err = pci_request_regions(pdev, NF10_DRV_NAME)))
		goto err_request_regions;

	pci_set_master(pdev);

	netdev = alloc_etherdev(sizeof(struct nf10_adapter));
	if (!netdev) {
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev_priv(netdev);
	pci_set_drvdata(pdev, adapter);

	adapter->netdev = netdev;
	adapter->pdev = pdev;
	adapter->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);
	if ((adapter->bar0 = pci_iomap(pdev, 0, 0)) == NULL) {
		err = -EIO;
		goto err_pci_iomap_bar0;
	}

	if ((adapter->bar2 = pci_iomap(pdev, 2, 0)) == NULL) {
		err = -EIO;
		goto err_pci_iomap_bar2;
	}

	if ((err = pci_enable_msi(pdev))) {
		pr_err("failed to enable MSI: err=%d\n", err);
		goto err_enable_msi;
	}

	if ((err = nf10_init_phy(pdev))) {
		pr_err("failed to initialize PHY chip\n");
		goto err_init_phy;
	}

	if ((err = request_irq(pdev->irq, nf10_interrupt_handler, 0, 
					NF10_DRV_NAME, pdev))) {
		pr_err("failed to request irq%d\n", pdev->irq);
		goto err_request_irq;
	}

	/* FIXME: currently, we assume only one port of nf10. if we use
	 * all four, allocate netdev for each port (interface) */
	/* TODO: other features & watchdog setting */
	netdev->netdev_ops = &nf10_netdev_ops;
	nf10_set_ethtool_ops(netdev);
	strcpy(netdev->name, "nf%d");
	memcpy(netdev->dev_addr, &nf10_test_dev_addr, ETH_ALEN);
	if ((err = register_netdev(netdev))) {
		pr_err("failed to register netdev\n");
		goto err_register_netdev;
	}

	if ((err = nf10_init(adapter))) {
		pr_err("failed to register hw ops\n");
		goto err_register_hw_ops;
	}

	/* direct user access */
	nf10_init_fops(adapter);
	init_waitqueue_head(&adapter->wq_user_intr);

	netif_napi_add(netdev, &adapter->napi, nf10_poll,
		       nf10_napi_budget(adapter));
	napi_enable(&adapter->napi);

#ifdef CONFIG_XEN_NF_BACKEND
	if (xen_nfback_init())
		pr_warn("failed to init xen nfback\n");
#endif

	netif_info(adapter, probe, netdev, "probe is done successfully\n");

	return 0;

err_register_hw_ops:
	unregister_netdev(netdev);
err_register_netdev:
	free_irq(pdev->irq, pdev);
err_request_irq:
err_init_phy:
	pci_disable_msi(pdev);
err_enable_msi:
	pci_iounmap(pdev, adapter->bar2);
err_pci_iomap_bar2:
	pci_iounmap(pdev, adapter->bar0);
err_pci_iomap_bar0:
	free_netdev(netdev);
	pci_set_drvdata(pdev, NULL);
err_alloc_etherdev:
	pci_clear_master(pdev);
	pci_release_regions(pdev);
err_request_regions:
err_dma:
	pci_disable_device(pdev);
	return err;
}

static void nf10_remove(struct pci_dev *pdev)
{
	struct nf10_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev;

	if (!adapter)
		return;

	netdev = adapter->netdev;

#ifdef CONFIG_XEN_NF_BACKEND
	xen_nfback_fini();
#endif
	nf10_remove_fops(adapter);
	netif_napi_del(&adapter->napi);
	napi_disable(&adapter->napi);
	nf10_free(adapter);
        unregister_netdev(netdev);

	free_irq(pdev->irq, pdev);
	pci_disable_msi(pdev);
	pci_iounmap(pdev, adapter->bar2);
	pci_iounmap(pdev, adapter->bar0);
	free_netdev(netdev);
	pci_set_drvdata(pdev, NULL);
	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	netif_info(adapter, probe, netdev, "remove is done successfully\n");
}

static struct pci_device_id pci_id[] = {
	{PCI_DEVICE(NF10_VENDOR_ID, NF10_DEVICE_ID)},
	{0}
};
MODULE_DEVICE_TABLE(pci, pci_id);

pci_ers_result_t nf10_pcie_error(struct pci_dev *dev, 
				 enum pci_channel_state state)
{
	/* TODO */
	return PCI_ERS_RESULT_RECOVERED;
}

static struct pci_error_handlers pcie_err_handlers = {
	.error_detected = nf10_pcie_error
};

static struct pci_driver pci_driver = {
	.name = NF10_DRV_NAME,
	.id_table = pci_id,
	.probe = nf10_probe,
	.remove = nf10_remove,
	.err_handler = &pcie_err_handlers
};

module_pci_driver(pci_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Cambridge NaaS Team");
MODULE_DESCRIPTION("Device driver for NetFPGA 10g reference NIC");
