
#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/types.h>        /* Needed for the macros */
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h> 
#include <linux/spinlock.h> 
#include <asm/cacheflush.h>
#include <linux/etherdevice.h>
#include "my_driver.h"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Cambridge NaaS Team");
MODULE_DESCRIPTION("A simple approach");	/* What does this module do */

static struct pci_device_id pci_id[] = {
    {PCI_DEVICE(XILINX_VENDOR_ID, MY_APPLICATION_ID)},
    {0}
};
MODULE_DEVICE_TABLE(pci, pci_id);

static DEFINE_SPINLOCK(my_lock);


// This function process the packets in the standard way that delivers sk_buff to the kernel stack.
// We could instead deliver the huge page back to user space and process it there.
// At this point the packets are in system memory anyway; moreover, huge pages cannot be swapped 
// from system memory.
void rx_wq_function(struct work_struct *wk) {
    struct my_driver_host_data *my_drv_data = ((struct my_work_t *)wk)->my_drv_data_ptr;
    struct sk_buff *my_skb;
    //struct skb_shared_hwtstamps *my_hwtstamps;        // To be used in another implementation
    //u32 pkt_ts_sec;                                   // 
    //u32 pkt_ts_nsec;
    int dw_index;
    int dw_max_index;
    unsigned long interrupt_status;
    u32 *current_hp_addr;
    u8  current_hp;
    u32 pkt_len;
    u32 numb_of_qwords;
    u8 bytes_remainder;
    #ifdef MY_DEBUG
    int pkt_counter = 0;
    #endif

    spin_lock_irqsave(&my_lock, interrupt_status);

    if (my_drv_data->huge_page_index == 2) {    // Proccess Huge Page 1
        current_hp = 1;
        pci_dma_sync_single_for_cpu(my_drv_data->pdev, my_drv_data->huge_page1_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        current_hp_addr = (u32 *)my_drv_data->huge_page_kern_address1;
    }
    else if (my_drv_data->huge_page_index == 3) {                                  // Proccess Huge Page 2
        current_hp = 2;
        pci_dma_sync_single_for_cpu(my_drv_data->pdev, my_drv_data->huge_page2_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        current_hp_addr = (u32 *)my_drv_data->huge_page_kern_address2;
    }
    else if (my_drv_data->huge_page_index == 4) {                                  // Proccess Huge Page 2
        current_hp = 3;
        pci_dma_sync_single_for_cpu(my_drv_data->pdev, my_drv_data->huge_page3_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        current_hp_addr = (u32 *)my_drv_data->huge_page_kern_address3;
    }
    else {                                  // Proccess Huge Page 4
        current_hp = 4;
        pci_dma_sync_single_for_cpu(my_drv_data->pdev, my_drv_data->huge_page4_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        current_hp_addr = (u32 *)my_drv_data->huge_page_kern_address4;
    }
    //clflush_cache_range(current_hp_addr, 2*1024*1024);
    
    numb_of_qwords = current_hp_addr[0];            //DW0 contains this information
    dw_max_index = (numb_of_qwords << 1) + 32;      // DWs = QWs*2. Header offset

    #ifdef MY_DEBUG
    printk(KERN_INFO "Myd: HW wrote %d QWs\n", (int)numb_of_qwords);
    if (!numb_of_qwords) {printk(KERN_INFO "Myd: Something happend and I received an empty huge page\n"); goto proccesing_finished;}
    else if (dw_max_index > 524288) {printk(KERN_INFO "Myd: Something happend and I received an overwritten huge page\n"); goto proccesing_finished;}
    #endif

    //DW01 to DW31 (both included) are reserved
    dw_index = 32;

    do {
        dw_index++;                                                             // DW reserved for timestamp

        pkt_len = current_hp_addr[dw_index];dw_index++;                         //DW contains the lenght of the packet in bytes.

        my_skb = netdev_alloc_skb(my_drv_data->my_net_device, pkt_len - 4);
        if (!my_skb) {printk(KERN_ERR "Myd: failed netdev_alloc_skb: pkt_len = %d\n", pkt_len);goto next_pkt;} // to do: increment statistics dropped

        // it would be nice to copy to processor's cache :p
        memcpy(my_skb->data, (void *)(current_hp_addr + dw_index), pkt_len - 4);

        skb_put(my_skb, pkt_len - 4);     // increase skb->tail pkt_len bytes

        my_skb->protocol = eth_type_trans(my_skb, my_drv_data->my_net_device);
        my_skb->ip_summed = CHECKSUM_NONE;

        // my_hwtstamps = skb_hwtstamps(my_skb);   //obtain a pointer to the structure
        // memset(my_hwtstamps, 0, sizeof(struct skb_shared_hwtstamps));       //intel driver sets the struct to 0
        // my_hwtstamps->hwtstamp = ktime_set(pkt_ts_sec, pkt_ts_nsec);        //timestamp the skb
        
        netif_receive_skb(my_skb);  //dev_kfree_skb(my_skb);
        my_drv_data->my_net_device->stats.rx_packets++;
        
        #ifdef MY_DEBUG
        pkt_counter++;
        #endif
       
next_pkt:
        dw_index += pkt_len >> 2;   // divide by 4
        
        bytes_remainder = pkt_len & 0x7;
        if (bytes_remainder >= 4) {dw_index++;}
        else if (bytes_remainder > 0) {dw_index +=2;}
        
    } while(dw_index < dw_max_index);

    #ifdef MY_DEBUG
proccesing_finished:
    my_drv_data->total_numb_of_huge_pages_processed++;
    printk(KERN_INFO "Myd: total_numb_of_huge_pages_processed: %d\n", (int)my_drv_data->total_numb_of_huge_pages_processed);
    printk(KERN_INFO "Myd: pkt_counter in current huge page: %d\n", (int)pkt_counter);
    #endif
    
    // Send Memory Write Request TLPs with huge pages' card-lock-up
    if (current_hp == 1) {     // Return Huge Page 1
        pci_dma_sync_single_for_device(my_drv_data->pdev, my_drv_data->huge_page1_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        //atomic_set(&my_drv_data->huge_page1_ready, 1);
    }
    else if (current_hp == 2) {                 // Return Huge Page 2
        pci_dma_sync_single_for_device(my_drv_data->pdev, my_drv_data->huge_page2_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        //atomic_set(&my_drv_data->huge_page2_ready, 1);
    }
    else if (current_hp == 3) {        
        pci_dma_sync_single_for_device(my_drv_data->pdev, my_drv_data->huge_page3_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        //atomic_set(&my_drv_data->huge_page3_ready, 1);
    }
    else {               
        pci_dma_sync_single_for_device(my_drv_data->pdev, my_drv_data->huge_page4_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        //atomic_set(&my_drv_data->huge_page4_ready, 1);
    }

    spin_unlock_irqrestore(&my_lock, interrupt_status);

    //return;
}

irqreturn_t card_interrupt_handler(int irq, void *dev_id) {
    struct pci_dev *pdev = dev_id;
    struct my_driver_host_data *my_drv_data = (struct my_driver_host_data *)pci_get_drvdata(pdev);
    int timeout = 0;
    int ret;

    do {
        ret = queue_work(my_drv_data->rx_wq, (struct work_struct *)&my_drv_data->rx_work);                            //Process Huge Page on kernel thread
        //timeout++;
    //} while (!ret && (timeout < 100));
    } while (!ret);

    if (my_drv_data->huge_page_index == 1) {    // Proccess Huge Page 1
        my_drv_data->huge_page_index = 2;   //toogle index
        //if (atomic_read(&my_drv_data->huge_page3_ready)) {
        //    atomic_set(&my_drv_data->huge_page3_ready, 0);
            *(((u64 *)my_drv_data->bar2) + 1) = my_drv_data->huge_page3_dma_addr;
            *(((u32 *)my_drv_data->bar2) + 6) = 0xcacabeef;
       // }
    }
    else if (my_drv_data->huge_page_index == 2){                                  // Proccess Huge Page 2
        my_drv_data->huge_page_index = 3;   //toogle index
       // if (atomic_read(&my_drv_data->huge_page4_ready)) {
       //     atomic_set(&my_drv_data->huge_page4_ready, 0);
            *(((u64 *)my_drv_data->bar2) + 2) = my_drv_data->huge_page4_dma_addr;
            *(((u32 *)my_drv_data->bar2) + 7) = 0xcacabeef;
      //  }
    }
    else if (my_drv_data->huge_page_index == 3){                                  // Proccess Huge Page 2
        my_drv_data->huge_page_index = 4;   //toogle index
      //  if (atomic_read(&my_drv_data->huge_page1_ready)) {
     //       atomic_set(&my_drv_data->huge_page1_ready, 0);
            *(((u64 *)my_drv_data->bar2) + 1) = my_drv_data->huge_page1_dma_addr;
            *(((u32 *)my_drv_data->bar2) + 6) = 0xcacabeef;
      //  }
    }
    else if (my_drv_data->huge_page_index == 4){                                  // Proccess Huge Page 2
        my_drv_data->huge_page_index = 1;   //toogle index
     //   if (atomic_read(&my_drv_data->huge_page2_ready)) {
     //       atomic_set(&my_drv_data->huge_page2_ready, 0);
            *(((u64 *)my_drv_data->bar2) + 2) = my_drv_data->huge_page2_dma_addr;
            *(((u32 *)my_drv_data->bar2) + 7) = 0xcacabeef;
      //  }
    }

    // if (!ret) {
    //     printk(KERN_INFO "busy\n");
    // }

    #ifdef MY_DEBUG
    printk(KERN_INFO "Myd: Interruption received\n");
    #endif

    return IRQ_HANDLED;
}

static int my_net_device_open(struct net_device *my_net_device) {
    printk(KERN_INFO "Myd: my_net_device_open\n");
    return 0;
}

static int my_net_device_close(struct net_device *my_net_device) {
    printk(KERN_INFO "Myd: my_net_device_close\n");
    return 0;
}

static const struct net_device_ops my_net_device_ops = {
    .ndo_open           = my_net_device_open,
    .ndo_stop           = my_net_device_close
    // .ndo_start_xmit     = rtl8169_start_xmit
    // .ndo_get_stats64    = rtl8169_get_stats64,
    // .ndo_tx_timeout     = rtl8169_tx_timeout,
    // .ndo_validate_addr  = eth_validate_addr,
    // .ndo_change_mtu     = rtl8169_change_mtu,
    // .ndo_fix_features   = rtl8169_fix_features,
    // .ndo_set_features   = rtl8169_set_features,
    // .ndo_set_mac_address    = rtl_set_mac_address,
    // .ndo_do_ioctl       = rtl8169_ioctl,
    // .ndo_set_rx_mode    = rtl_set_rx_mode,
    // .ndo_poll_controller    = rtl8169_netpoll,
};

static inline int my_linux_network_interface(struct my_driver_host_data *my_drv_data) {
    int ret;
    u64 my_mac_invented_addr = 0x000f530dd165;

    my_drv_data->my_net_device = alloc_etherdev(sizeof(int));
    if (my_drv_data->my_net_device == NULL) {printk(KERN_ERR "Myd: failed alloc_netdev\n"); return -1;}

    printk(KERN_INFO "Myd: alloc_netdev\n");
    my_drv_data->my_net_device->netdev_ops = &my_net_device_ops;
    memcpy(my_drv_data->my_net_device->dev_addr, &my_mac_invented_addr, ETH_ALEN);

    ret = register_netdev(my_drv_data->my_net_device);
    if (ret) {printk(KERN_ERR "Myd: failed register_netdev\n"); return ret;}

    return 0;
}

static int my_pcie_probe(struct pci_dev *pdev, const struct pci_device_id *id) {
    int ret = -ENODEV;
    struct my_driver_host_data *my_drv_data;
    struct page *aux_huge_page;

    #ifdef MY_DEBUG
    printk(KERN_INFO "Myd: pcie card with VENDOR_ID:SYSTEM_ID matched with this module advertised systems support\n");
    #endif
    
    my_drv_data = kzalloc(sizeof(struct my_driver_host_data), GFP_KERNEL);          //use vmalloc?
    if (my_drv_data == NULL) {printk(KERN_ERR "Myd: failed to alloc system mem\n"); return ret;}

    my_drv_data->pdev = pdev;       // Save a pointer (struct pci_dev *) to the FPGA card (the PCIe device)
    
    /*    
    * Rx work queue. If the huge page content is intended to be processed by kernel thread(s)
    * Note that more workqueues could be allocated if one processor is not enough.
    * There is much to do in order to improve host processing speed.
    */
    my_drv_data->rx_wq = alloc_workqueue("rx_wq", WQ_HIGHPRI, 0);         // Change to WQ_MEM_RECLAIM or WQ_HIGHPRI
    if (!my_drv_data->rx_wq) {printk(KERN_ERR "Myd: alloc work queue\n"); goto err_01;}
    INIT_WORK((struct work_struct *)&my_drv_data->rx_work, rx_wq_function);
    my_drv_data->rx_work.my_drv_data_ptr = my_drv_data;

    // PCIe typical initialization sequence 
    ret = pci_enable_device(pdev);
    if (ret) {printk(KERN_ERR "Myd: pci_enable_device\n"); goto err_02;}

    ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
    if (ret) {printk(KERN_ERR "Myd: pci_set_dma_mask\n"); goto err_03;}
    ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
    if (ret) {printk(KERN_ERR "Myd: pci_set_consistent_dma_mask\n"); goto err_03;}

    pci_set_drvdata(pdev, my_drv_data);

    ret = pci_request_regions(pdev, DRV_NAME);
    if (ret) {printk(KERN_ERR "Myd: pci_request_regions\n"); goto err_04;}

    my_drv_data->bar2 = pci_iomap(pdev, PCI_BAR2, pci_resource_len(pdev, PCI_BAR2));             // BAR2 used to communicate huge page ownership
    if (my_drv_data->bar2 == NULL) {printk(KERN_ERR "Myd: pci_iomap bar2\n"); goto err_05;}

    my_drv_data->bar0 = pci_iomap(pdev, PCI_BAR0, pci_resource_len(pdev, PCI_BAR0));            // BAR0 used to configure AEL2005 PHY chips
    if (my_drv_data->bar0 == NULL) {printk(KERN_ERR "Myd: pci_iomap bar0\n"); goto err_06;}

    pci_set_master(pdev);

    ret = pci_enable_msi(pdev);
    if (ret) {printk(KERN_ERR "Myd: pci_enable_msi\n"); goto err_07;}

    // AEL2005 MDIO configuration
    ret = request_irq(pdev->irq, mdio_access_interrupt_handler, 0, DRV_NAME, pdev);             // with MSI in linux we cannot allocate more than one vector. use the interrupt line with this function during intialization
    if (ret) {printk(KERN_ERR "Myd: request_irq\n"); goto err_08;}
    
    ret = configure_ael2005_phy_chips(my_drv_data);
    if (ret) {printk(KERN_ERR "Myd: warning, AEL2005 not configured\n");}
 
    free_irq(pdev->irq, pdev);
    // AEL2005 MDIO configuration ready

    ret = request_irq(pdev->irq, card_interrupt_handler, 0, DRV_NAME, pdev);                    // The interrupt line is asserted when a new huge page is ready to be processed
    if (ret) {printk(KERN_ERR "Myd: request_irq\n"); goto err_08;}

    // Reserve Huge Pages
    my_drv_data->huge_page1 = alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
    if (my_drv_data->huge_page1 == NULL) {printk(KERN_ERR "Myd: alloc huge page\n"); goto err_09;}
    my_drv_data->huge_page2 = alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
    if (my_drv_data->huge_page2 == NULL) {printk(KERN_ERR "Myd: alloc huge page\n"); goto err_10;}
    my_drv_data->huge_page3 = alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
    if (my_drv_data->huge_page3 == NULL) {printk(KERN_ERR "Myd: alloc huge page\n"); goto err_11;}
    my_drv_data->huge_page4 = alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
    if (my_drv_data->huge_page4 == NULL) {printk(KERN_ERR "Myd: alloc huge page\n"); goto err_12;}

    my_drv_data->huge_page_kern_address1 = (void *)page_address(my_drv_data->huge_page1);
    my_drv_data->huge_page_kern_address2 = (void *)page_address(my_drv_data->huge_page2);
    my_drv_data->huge_page_kern_address3 = (void *)page_address(my_drv_data->huge_page3);
    my_drv_data->huge_page_kern_address4 = (void *)page_address(my_drv_data->huge_page4);

    // Get huge pages' physical address
    my_drv_data->huge_page1_dma_addr = pci_map_single(pdev, page_address(my_drv_data->huge_page1), 2*1024*1024, PCI_DMA_FROMDEVICE);
    my_drv_data->huge_page2_dma_addr = pci_map_single(pdev, page_address(my_drv_data->huge_page2), 2*1024*1024, PCI_DMA_FROMDEVICE);
    my_drv_data->huge_page3_dma_addr = pci_map_single(pdev, page_address(my_drv_data->huge_page3), 2*1024*1024, PCI_DMA_FROMDEVICE);
    my_drv_data->huge_page4_dma_addr = pci_map_single(pdev, page_address(my_drv_data->huge_page4), 2*1024*1024, PCI_DMA_FROMDEVICE);

    /*#ifdef MY_DEBUG
    memset(my_drv_data->huge_page_kern_address1, 0, 2*1024*1024);
    memset(my_drv_data->huge_page_kern_address2, 0, 2*1024*1024);
    #endif*/

    /*
    The physical address must be a 64-bit address otherwise the hw gets to complex.
    How to make sure of this? maybe add the flag HIGH_MEM??
    Remove these while loops when you figure how to assure 64 bit address. 
    */
    while ( !(my_drv_data->huge_page1_dma_addr >> 32) ) {
        printk(KERN_INFO "Myd: ask for a new huge page 1\n");
        pci_unmap_single(pdev, my_drv_data->huge_page1_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        aux_huge_page = alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
        __free_pages(my_drv_data->huge_page1, HPAGE_PMD_ORDER);
        my_drv_data->huge_page1 = aux_huge_page;
        if (my_drv_data->huge_page1 == NULL) {printk(KERN_ERR "Myd: alloc huge page in loop\n"); goto err_13;}
        my_drv_data->huge_page_kern_address1 = (void *)page_address(my_drv_data->huge_page1);
        my_drv_data->huge_page1_dma_addr = pci_map_single(pdev, page_address(my_drv_data->huge_page1), 2*1024*1024, PCI_DMA_FROMDEVICE);
    }

    while ( !(my_drv_data->huge_page2_dma_addr >> 32) ) {
        printk(KERN_INFO "Myd: ask for a new huge page 2\n");
        pci_unmap_single(pdev, my_drv_data->huge_page2_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        aux_huge_page = alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
        __free_pages(my_drv_data->huge_page2, HPAGE_PMD_ORDER);
        my_drv_data->huge_page2 = aux_huge_page;
        if (my_drv_data->huge_page2 == NULL) {printk(KERN_ERR "Myd: alloc huge page in loop\n"); goto err_13;}
        my_drv_data->huge_page_kern_address2 = (void *)page_address(my_drv_data->huge_page2);
        my_drv_data->huge_page2_dma_addr = pci_map_single(pdev, page_address(my_drv_data->huge_page2), 2*1024*1024, PCI_DMA_FROMDEVICE);
    }

    while ( !(my_drv_data->huge_page3_dma_addr >> 32) ) {
        printk(KERN_INFO "Myd: ask for a new huge page 3\n");
        pci_unmap_single(pdev, my_drv_data->huge_page3_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        aux_huge_page = alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
        __free_pages(my_drv_data->huge_page3, HPAGE_PMD_ORDER);
        my_drv_data->huge_page3 = aux_huge_page;
        if (my_drv_data->huge_page3 == NULL) {printk(KERN_ERR "Myd: alloc huge page in loop\n"); goto err_13;}
        my_drv_data->huge_page_kern_address3 = (void *)page_address(my_drv_data->huge_page3);
        my_drv_data->huge_page3_dma_addr = pci_map_single(pdev, page_address(my_drv_data->huge_page3), 2*1024*1024, PCI_DMA_FROMDEVICE);
    }

    while ( !(my_drv_data->huge_page4_dma_addr >> 32) ) {
        printk(KERN_INFO "Myd: ask for a new huge page 4\n");
        pci_unmap_single(pdev, my_drv_data->huge_page4_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        aux_huge_page = alloc_pages(GFP_TRANSHUGE, HPAGE_PMD_ORDER);
        __free_pages(my_drv_data->huge_page4, HPAGE_PMD_ORDER);
        my_drv_data->huge_page4 = aux_huge_page;
        if (my_drv_data->huge_page4 == NULL) {printk(KERN_ERR "Myd: alloc huge page in loop\n"); goto err_13;}
        my_drv_data->huge_page_kern_address4 = (void *)page_address(my_drv_data->huge_page4);
        my_drv_data->huge_page4_dma_addr = pci_map_single(pdev, page_address(my_drv_data->huge_page4), 2*1024*1024, PCI_DMA_FROMDEVICE);
    }

    #ifdef MY_DEBUG
    printk(KERN_INFO "Myd: huge_page1 kernaddr: 0x%08x %08x\n", (int)((u64)page_address(my_drv_data->huge_page1) >> 32), (int)(u64)page_address(my_drv_data->huge_page1));
    printk(KERN_INFO "Myd: huge_page1 dma addr: 0x%08x %08x\n", (int)((u64)my_drv_data->huge_page1_dma_addr >> 32), (int)(u64)my_drv_data->huge_page1_dma_addr);
    printk(KERN_INFO "Myd: huge_page2 kernaddr: 0x%08x %08x\n", (int)((u64)page_address(my_drv_data->huge_page2) >> 32), (int)(u64)page_address(my_drv_data->huge_page2));
    printk(KERN_INFO "Myd: huge_page2 dma addr: 0x%08x %08x\n", (int)((u64)my_drv_data->huge_page2_dma_addr >> 32), (int)(u64)my_drv_data->huge_page2_dma_addr);
    printk(KERN_INFO "Myd: huge_page3 kernaddr: 0x%08x %08x\n", (int)((u64)page_address(my_drv_data->huge_page3) >> 32), (int)(u64)page_address(my_drv_data->huge_page3));
    printk(KERN_INFO "Myd: huge_page3 dma addr: 0x%08x %08x\n", (int)((u64)my_drv_data->huge_page3_dma_addr >> 32), (int)(u64)my_drv_data->huge_page3_dma_addr);
    printk(KERN_INFO "Myd: huge_page4 kernaddr: 0x%08x %08x\n", (int)((u64)page_address(my_drv_data->huge_page4) >> 32), (int)(u64)page_address(my_drv_data->huge_page4));
    printk(KERN_INFO "Myd: huge_page4 dma addr: 0x%08x %08x\n", (int)((u64)my_drv_data->huge_page4_dma_addr >> 32), (int)(u64)my_drv_data->huge_page4_dma_addr);
    #endif

    // Instantiate an ethX interface in linux
    ret = my_linux_network_interface(my_drv_data);
    if (ret) {printk(KERN_ERR "Myd: my_linux_network_interface\n"); goto err_11;}

    atomic_set(&my_drv_data->huge_page1_ready, 0);
    atomic_set(&my_drv_data->huge_page2_ready, 0);
    atomic_set(&my_drv_data->huge_page3_ready, 1);
    atomic_set(&my_drv_data->huge_page4_ready, 1);
    // Ready to start operation

    // Send Memory Write Request TLPs with huge pages' address
    *(((u64 *)my_drv_data->bar2) + 1) = my_drv_data->huge_page1_dma_addr;
    *(((u64 *)my_drv_data->bar2) + 2) = my_drv_data->huge_page2_dma_addr;

    // Send Memory Write Request TLPs with huge pages' card-lock-up
    *(((u32 *)my_drv_data->bar2) + 6) = 0xcacabeef;
    *(((u32 *)my_drv_data->bar2) + 7) = 0xcacabeef;

    my_drv_data->huge_page_index = 1;

    #ifdef MY_DEBUG
    printk(KERN_INFO "Myd: my_pcie_probe finished\n");
    #endif
    return ret;

err_13:
    __free_pages(my_drv_data->huge_page4, HPAGE_PMD_ORDER);
err_12:
    __free_pages(my_drv_data->huge_page3, HPAGE_PMD_ORDER);
err_11:
    __free_pages(my_drv_data->huge_page2, HPAGE_PMD_ORDER);
err_10:
    __free_pages(my_drv_data->huge_page1, HPAGE_PMD_ORDER);
err_09:
    free_irq(pdev->irq, pdev);
err_08:
    pci_disable_msi(pdev);
err_07:
    pci_clear_master(pdev);
    pci_iounmap(pdev, my_drv_data->bar0);
err_06:
    pci_iounmap(pdev, my_drv_data->bar2);
err_05:
    pci_release_regions(pdev);
err_04:
    pci_set_drvdata(pdev, NULL);
err_03:
    pci_disable_device(pdev);
err_02:
    destroy_workqueue(my_drv_data->rx_wq);
err_01:
    kfree(my_drv_data);
    return ret;
}

static void my_pcie_remove(struct pci_dev *pdev) {
    struct my_driver_host_data *my_drv_data;
    
    printk(KERN_INFO "Myd: entering my_pcie_remove\n");
    my_drv_data = (struct my_driver_host_data *)pci_get_drvdata(pdev);
    if (my_drv_data) {
        free_irq(pdev->irq, pdev);
        flush_workqueue(my_drv_data->rx_wq);
        destroy_workqueue(my_drv_data->rx_wq);
        
        pci_unmap_single(pdev, my_drv_data->huge_page2_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        pci_unmap_single(pdev, my_drv_data->huge_page1_dma_addr, 2*1024*1024, PCI_DMA_FROMDEVICE);  // unmap page
        __free_pages(my_drv_data->huge_page2, HPAGE_PMD_ORDER);
        __free_pages(my_drv_data->huge_page1, HPAGE_PMD_ORDER);

        unregister_netdev(my_drv_data->my_net_device);
        free_netdev(my_drv_data->my_net_device);

        pci_disable_msi(pdev);
        pci_clear_master(pdev);
        pci_iounmap(pdev, my_drv_data->bar0);
        pci_iounmap(pdev, my_drv_data->bar2);
        pci_release_regions(pdev);
        pci_set_drvdata(pdev, NULL);
        pci_disable_device(pdev);

        kfree(my_drv_data);
        #ifdef MY_DEBUG
        printk(KERN_INFO "Myd: my_pcie_remove realeased resources\n");
        #endif
    }
}

pci_ers_result_t my_pcie_error(struct pci_dev *dev, enum pci_channel_state state) {
    printk(KERN_ALERT "Myd: PCIe error: %d\n", state);
    return PCI_ERS_RESULT_RECOVERED;
}

static struct pci_error_handlers pcie_err_handlers = {
    .error_detected = my_pcie_error
};

static struct pci_driver pci_driver = {
    .name = DRV_NAME,
    .id_table = pci_id,
    .probe = my_pcie_probe,
    .remove = my_pcie_remove,
    .err_handler = &pcie_err_handlers//,
    //.suspend = my_suspend,
    //.resume = my_resume
};

module_pci_driver(pci_driver);
