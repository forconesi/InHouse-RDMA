#include "nf10.h"
#include "nf10_user.h"

static dev_t devno;
static struct class *dev_class;

static int nf10_open(struct inode *n, struct file *f)
{
	struct nf10_adapter *adapter = (struct nf10_adapter *)container_of(
					n->i_cdev, struct nf10_adapter, cdev);
	if (adapter->user_ops == NULL) {
		netif_err(adapter, drv, adapter->netdev,
				"no user_ops is set\n");
		return -EINVAL;
	}
	f->private_data = adapter;
	return 0;
}

static int nf10_mmap(struct file *f, struct vm_area_struct *vma)
{
	struct nf10_adapter *adapter = f->private_data;
	unsigned long pfn;
	unsigned long size;
	int err = 0;
	
	if ((vma->vm_start & ~PAGE_MASK) || (vma->vm_end & ~PAGE_MASK)) {
		netif_err(adapter, drv, adapter->netdev,
			  "not aligned vaddrs (vm_start=%lx vm_end=%lx)", 
			  vma->vm_start, vma->vm_end);
		return -EINVAL;
	}

	size = vma->vm_end - vma->vm_start;
	/* TODO: some arg/bound checking: 
	 * size must be the same as kernel buffer */

	pfn = adapter->user_ops->get_pfn(adapter, adapter->nr_user_mmap);

	err = remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot);

	netif_dbg(adapter, drv, adapter->netdev,
		  "mmapped [%d] err=%d va=%p pfn=%lx size=%lu\n",
		  adapter->nr_user_mmap, err, (void *)vma->vm_start, pfn, size);

	if (!err)
		adapter->nr_user_mmap++;

	return err;
}

static long nf10_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct nf10_adapter *adapter = (struct nf10_adapter *)f->private_data;

	switch(cmd) {
	case NF10_IOCTL_CMD_INIT:
	{
		u64 cons;
		cons = adapter->user_ops->init(adapter);
		if (copy_to_user((void __user *)arg, &cons, sizeof(u64)))
			return -EFAULT;
		break;
	}
	case NF10_IOCTL_CMD_PREPARE_RX:
		adapter->user_ops->prepare_rx_buffer(adapter, arg);
		break;
	case NF10_IOCTL_CMD_WAIT_INTR:
	{
		DEFINE_WAIT(wait);
		prepare_to_wait(&adapter->wq_user_intr, &wait,
				TASK_INTERRUPTIBLE);
		io_schedule();
		finish_wait(&adapter->wq_user_intr, &wait);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int nf10_release(struct inode *n, struct file *f)
{
	f->private_data = NULL;
	return 0;
}

static struct file_operations nf10_fops = {
	.owner = THIS_MODULE,
	.open = nf10_open,
	.mmap = nf10_mmap,
	.unlocked_ioctl = nf10_ioctl,
	.release = nf10_release
};

int nf10_init_fops(struct nf10_adapter *adapter)
{
	int err;

	if ((err = alloc_chrdev_region(&devno, 0, 1, NF10_DRV_NAME))) {
		netif_err(adapter, probe, adapter->netdev,
				"failed to alloc chrdev\n");
		return err;
	}
	cdev_init(&adapter->cdev, &nf10_fops);
	adapter->cdev.owner = THIS_MODULE;
	adapter->cdev.ops = &nf10_fops;
	if ((err = cdev_add(&adapter->cdev, devno, 1))) {
		netif_err(adapter, probe, adapter->netdev,
			  "failed to add cdev\n");
		return err;
	}

	dev_class = class_create(THIS_MODULE, NF10_DRV_NAME);
	device_create(dev_class, NULL, devno, NULL, NF10_DRV_NAME);
	return 0;
}

int nf10_remove_fops(struct nf10_adapter *adapter)
{
	device_destroy(dev_class, devno);
	class_unregister(dev_class);
	class_destroy(dev_class);
	cdev_del(&adapter->cdev);
	unregister_chrdev_region(devno, 1);

	return 0;
}

bool nf10_user_rx_callback(struct nf10_adapter *adapter)
{
	/* if direct user access mode is enabled, just wake up
	 * a waiting user thread */
	if (adapter->nr_user_mmap > 0) { 
		if (likely(waitqueue_active(&adapter->wq_user_intr)))
			wake_up(&adapter->wq_user_intr);
		/* in case a user thread has mapped rx buffers, but
		 * not waiting for an interrupt, just skip it while granting
		 * an opportunity for the thread to poll buffers later */
		return true;
	}
	return false;
}
