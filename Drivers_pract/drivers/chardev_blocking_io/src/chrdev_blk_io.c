#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/spinlock.h>


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel module demonstrating blocking and non-blocking i/o");
MODULE_AUTHOR("Firefly24");


struct chrdev_state{
	bool data_available;
	bool terminating;
	
	wait_queue_head_t wq;
	spinlock_t state_lock;
	
	dev_t device_num;
	struct cdev cdev;
	struct class *chdev_class;
	struct device *ch_device;
};


static struct chrdev_state dev; 


/*--------------File operations----------------------------------*/

static int chrdev_blk_io_open(struct inode *filenode, struct file *dev_file)
{
	return 0;
}

static int chrdev_blk_io_release(struct inode *filenode, struct file *dev_file)
{
	return 0;
}

static ssize_t chrdev_blk_io_read(struct file *dev_file, 
				  char __user *usr_buf, 
				  size_t data_size, 
				  loff_t *offset)
{
	int ret = 0;

	// keep looping to wait for data to be available
	for (;;)
	{
	
		spin_lock(&dev.state_lock);
		
		// handle teardown on termination
		if(dev.terminating)
		{
			spin_unlock(&dev.state_lock);
			// teardown logic here
			return -ENODEV;
		}
		
		// Validate if data is available
		if (dev.data_available)
		{
			dev.data_available = false;
			spin_unlock(&dev.state_lock);
			
			// Do the data transfer here
			return data_size;
		}
		
		spin_unlock(&dev.state_lock);
		
		// Sleep process again till state may have changed
		ret = wait_event_interruptible(dev.wq, dev.data_available|| dev.terminating);
		
		if (ret)
		{
			pr_info("Read interrupted by a signal\n");
			return ret;
		}
		
		
	}
	return data_size;
}


static ssize_t chrdev_blk_io_write(struct file *dev_file,
				   const char __user *usr_buf,
				   size_t data_size,
				   loff_t *offset)
{

	spin_lock(&dev.state_lock);
	if (dev.terminating)
	{
		spin_unlock(&dev.state_lock);
		return -ENODEV;
	}
	spin_unlock(&dev.state_lock);
	
	// data available, write data to kernel buffer here
	
	spin_lock(&dev.state_lock);
	dev.data_available = true;
	spin_unlock(&dev.state_lock);
	
	wake_up_all(&dev.wq);
	
	return data_size;
}

static const struct file_operations chrdev_blk_io_fops = {
	.owner  = THIS_MODULE,
	.open = chrdev_blk_io_open,
	.release = chrdev_blk_io_release ,
	.read = chrdev_blk_io_read,
	.write = chrdev_blk_io_write,
};


/*----------- Module init/exit-----------------------------------*/

static int __init chrdev_blk_init(void)
{
	int ret = 0;
	
	dev.data_available = false;
	dev.terminating = false;
	
	init_waitqueue_head(&dev.wq);
	spin_lock_init(&dev.state_lock);
	
	
	if ( (ret = alloc_chrdev_region(&dev.device_num,0,1,"chrdev_io") ) )
		goto cleanup;
		
	// cdev creation
	cdev_init(&dev.cdev,&chrdev_blk_io_fops);
	dev.cdev.owner = THIS_MODULE;
	
	if ( (ret = cdev_add(&dev.cdev,dev.device_num,1)) )
		goto unreg_chrdev;
	
	// class creation
	dev.chdev_class = class_create("chrdev_io");
	if (IS_ERR_OR_NULL(dev.chdev_class))
	{
		ret = PTR_ERR(dev.chdev_class);
		goto del_cdev;
	}
	
	// device creation
	dev.ch_device = device_create(dev.chdev_class,NULL, dev.device_num, NULL, "chrdev_io");
	if(IS_ERR(dev.ch_device))
	{
		ret = PTR_ERR(dev.ch_device);
		goto del_class;
	}
	
	
	
	pr_info("Blocking i/o character device created successfully\n");
	
	return 0;
	
//del_device:
//	device_destroy(dev.chdev_class, dev.device_num);

del_class:
	class_destroy(dev.chdev_class);	
del_cdev:
	cdev_del(&dev.cdev);
unreg_chrdev:
	unregister_chrdev_region(dev.device_num,1);
cleanup:

	return ret;
}


static void __exit chrdev_blk_exit(void)
{
	// set terminating condition to true
	spin_lock(&dev.state_lock);
	dev.terminating  = true;
	spin_unlock(&dev.state_lock);
	
	// wake up all sleeping readers to execute termination cleanup
	wake_up_all(&dev.wq);
	
	// tear down device file
	device_destroy(dev.chdev_class, dev.device_num);
	class_destroy(dev.chdev_class);
	cdev_del(&dev.cdev);
	unregister_chrdev_region(dev.device_num,1);
}


module_init(chrdev_blk_init);
module_exit(chrdev_blk_exit);



