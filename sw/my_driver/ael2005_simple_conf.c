
#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/types.h>        /* Needed for the macros */
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h> 
#include "my_driver.h"
#include "ael2005_simple_conf.h"

irqreturn_t mdio_access_interrupt_handler(int irq, void *dev_id) {
    struct pci_dev *pdev = dev_id;
    struct my_driver_host_data *my_drv_data = (struct my_driver_host_data *)pci_get_drvdata(pdev);

    atomic_set(&my_drv_data->mdio_access_rdy, 1);           // Signals mdio write finished

    return IRQ_HANDLED;
}

int configure_ael2005_phy_chips(struct my_driver_host_data *my_drv_data) {
    int ret = -ENODEV;
    u8 OP_CODE;
    u8 PRTAD;
    u8 DEVAD;
    u16 ADDR, WRITE_DATA;
    int size, i;
    int timeout = 0;

    printk(KERN_INFO "Myd: AEL2005 Initialization Start...\n");

    // We only need interface 3 in this project
    for(PRTAD=3; PRTAD < 4; PRTAD++) {                                                                          // don't forget that the nearest port to PCIe is hardwired to 0x2
        DEVAD = MDIO_MMD_PMAPMD;
        
        // Step 1: write reset registers
        size = sizeof(reset) / sizeof(u16);
        for(i = 0; i < size; i+=2) {
            
            // MDIO clause 45 Set Address Transaction
            atomic_set(&my_drv_data->mdio_access_rdy, 0);
            OP_CODE = 0x0;
            ADDR = reset[i];
            *(((u32 *)my_drv_data->bar0) + 4) = (OP_CODE << 26) | (PRTAD << 21) | (DEVAD << 16) | ADDR;         // Send Memory Write Request TLP
            do {
                msleep(2);
                timeout++;
                if (timeout > 20) {return ret;}                                                                  // if it takes too long the hw is not working properly
            } while (!atomic_read(&my_drv_data->mdio_access_rdy));
        
            // MDIO clause 45 Write Transaction
            atomic_set(&my_drv_data->mdio_access_rdy, 0);
            OP_CODE = 0x1;
            WRITE_DATA = reset[i+1];
            *(((u32 *)my_drv_data->bar0) + 4) = (OP_CODE << 26) | (PRTAD << 21) | (DEVAD << 16) | WRITE_DATA;   // Send Memory Write Request TLP
            do {msleep(2);} while (!atomic_read(&my_drv_data->mdio_access_rdy));
        }

        // Step 2: write sr_edc or twinax_edc registers. (depending on the sfp+ modules)
        size = sizeof(sr_edc) / sizeof(u16);
        for(i = 0; i < size; i+=2) {
            
            // MDIO clause 45 Set Address Transaction
            atomic_set(&my_drv_data->mdio_access_rdy, 0);
            OP_CODE = 0x0;
            ADDR = sr_edc[i];
            *(((u32 *)my_drv_data->bar0) + 4) = (OP_CODE << 26) | (PRTAD << 21) | (DEVAD << 16) | ADDR;         // Send Memory Write Request TLP
            do {msleep(2);} while (!atomic_read(&my_drv_data->mdio_access_rdy));
        
            // MDIO clause 45 Write Transaction
            atomic_set(&my_drv_data->mdio_access_rdy, 0);
            OP_CODE = 0x1;
            WRITE_DATA = sr_edc[i+1];
            *(((u32 *)my_drv_data->bar0) + 4) = (OP_CODE << 26) | (PRTAD << 21) | (DEVAD << 16) | WRITE_DATA;   // Send Memory Write Request TLP
            do {msleep(2);} while (!atomic_read(&my_drv_data->mdio_access_rdy));
        }
        
        // Step 1: write regs1 registers
        size = sizeof(regs1) / sizeof(u16);
        for(i = 0; i < size; i+=2) {

            // MDIO clause 45 Set Address Transaction
            atomic_set(&my_drv_data->mdio_access_rdy, 0);
            OP_CODE = 0x0;
            ADDR = regs1[i];
            *(((u32 *)my_drv_data->bar0) + 4) = (OP_CODE << 26) | (PRTAD << 21) | (DEVAD << 16) | ADDR;         // Send Memory Write Request TLP
            do {msleep(2);} while (!atomic_read(&my_drv_data->mdio_access_rdy));
        
            // MDIO clause 45 Write Transaction
            atomic_set(&my_drv_data->mdio_access_rdy, 0);
            OP_CODE = 0x1;
            WRITE_DATA = regs1[i+1];
            *(((u32 *)my_drv_data->bar0) + 4) = (OP_CODE << 26) | (PRTAD << 21) | (DEVAD << 16) | WRITE_DATA;   // Send Memory Write Request TLP
            do {msleep(2);} while (!atomic_read(&my_drv_data->mdio_access_rdy));
        }
    } 

    printk(KERN_INFO "Myd: AEL2005 Initialization Finished...\n");

    return 0;
}
