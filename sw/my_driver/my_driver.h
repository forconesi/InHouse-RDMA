
#ifndef MY_DRIVER_H
#define MY_DRIVER_H

#define XILINX_VENDOR_ID 0x10EE
#define MY_APPLICATION_ID 0x4245
#define MY_DEBUG 1
#define DRV_NAME "my_driver"
#define PCI_BAR0 0
#define PCI_BAR2 2

irqreturn_t mdio_access_interrupt_handler(int irq, void *dev_id);

void rx_wq_function(struct work_struct *wk);

struct my_work_t {
    struct work_struct work;
    struct my_driver_host_data *my_drv_data_ptr;
};

struct my_driver_host_data {
    // Work queue things
    struct workqueue_struct *rx_wq;
    struct my_work_t rx_work;

    struct net_device *my_net_device;

    struct pci_dev *pdev;
    
    //Huge Pages things
    struct page *huge_page1;
    struct page *huge_page2;

    u64 huge_page1_dma_addr;
    u64 huge_page2_dma_addr;

    void *huge_page_kern_address1;
    void *huge_page_kern_address2;

    u32 huge_page_index;

    #ifdef MY_DEBUG
    int total_numb_of_huge_pages_processed;
    #endif

    void *bar2;
    void *bar0;

    atomic_t mdio_access_rdy;
};

int configure_ael2005_phy_chips(struct my_driver_host_data *my_drv_data);

#endif
