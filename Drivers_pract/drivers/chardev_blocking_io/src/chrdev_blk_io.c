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
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel module demonstrating blocking and non-blocking i/o");
MODULE_AUTHOR("Firefly24");

static int max_devices =1;
module_param(max_devices, int, 0644 );
MODULE_PARM_DESC(max_devices,"Maximum devices to be created");

#define FIFO_SIZE 1024
#define MAX_ALLOWED_DEVICES 10

//ioctl related definitions
#define CHRDEV_MAGIC 'C'
#define CHRDEV_TERM _IO(CHRDEV_MAGIC,1)

struct chrdev_driver
{
	dev_t base_dev;
	struct class *chdev_class;
	
	struct chrdev_device *devices;
};

struct chrdev_device{

	// device state related
	bool terminating;			// if device is being terminated
	spinlock_t state_lock;			// protect device state and data read/writes
	atomic_t active_refs;
	wait_queue_head_t term_wq;
	
	//queues for data and state
	wait_queue_head_t read_wq;		// wait_queue for readers
	wait_queue_head_t write_wq;		// wait_queue for writers
	struct kfifo fifo;			// fifo to hold written data
	
	//device node related 
	dev_t device_num;			// device number
	
	struct device *ch_device;
	struct cdev cdev;
};

static struct chrdev_driver g_drv;

/*--------------File operations----------------------------------*/

static int chrdev_blk_io_open(struct inode *filenode, struct file *dev_file)
{
	// use i_cdev as it is a object embedded in our device obj,
	// whereas with i_rdev, we need to manually lookup our device using minor number
	struct chrdev_device *dev = container_of(filenode->i_cdev,
			   struct chrdev_device,
			   cdev);
			   
	spin_lock(&dev->state_lock);
	if (dev->terminating) {
		spin_unlock(&dev->state_lock);
		return -ENODEV;
	}
	atomic_inc(&dev->active_refs);
	spin_unlock(&dev->state_lock);	   
						 
	dev_file->private_data = dev;
	
	return 0;
}

static int chrdev_blk_io_release(struct inode *filenode, struct file *dev_file)
{
	struct chrdev_device *dev = (struct chrdev_device *)dev_file->private_data;
	
	if (!dev)
		return -EINVAL;
	
	if(atomic_dec_and_test(&dev->active_refs))
		wake_up(&dev->term_wq);
	
	return 0;
}

static __poll_t chrdev_blk_io_poll(struct file *dev_file, struct poll_table_struct *polltable)
{
	__poll_t status =0;
	struct chrdev_device *dev = dev_file->private_data;
	
	// register waitqueue for sleeping
	poll_wait(dev_file,&dev->read_wq,polltable);
	poll_wait(dev_file,&dev->write_wq,polltable);
	
	// check state
	spin_lock(&dev->state_lock);
	
	if (dev->terminating)
		status |= POLLERR | POLLHUP; 
		
	if (!kfifo_is_empty(&dev->fifo))
		status |= POLLIN | POLLRDNORM;  // check poll() syscall manpage for these bits
		
	if (!kfifo_is_full(&dev->fifo))
		status |= POLLOUT | POLLWRNORM;
	
	spin_unlock(&dev->state_lock);
	
	return status;

}

static ssize_t chrdev_blk_io_read(struct file *dev_file, 
				  char __user *usr_buf, 
				  size_t data_size, 
				  loff_t *offset)
{
	
	unsigned int copied_data;
	int ret=0;
	struct chrdev_device *dev = dev_file->private_data;
	
	// keep looping to wait for data to be available
	for (;;)
	{
		spin_lock(&dev->state_lock);
		
		// handle teardown on termination
		if(dev->terminating)
		{
			spin_unlock(&dev->state_lock);
			pr_debug("Exiting read on termination\n");
			// teardown logic here
			return -ENODEV;
		}
		
		// Validate if data is available
		if (!kfifo_is_empty(&dev->fifo))
		{
			//dev.data_available = false;
			
			size_t data_avail = min(data_size,(size_t)kfifo_len(&dev->fifo));
			
			ret = kfifo_to_user(&dev->fifo, usr_buf, data_avail,&copied_data);
			
			spin_unlock(&dev->state_lock);
			
			wake_up_interruptible(&dev->write_wq);
			
			return ret? ret: copied_data;
		}
		
		// For non-blocking I/O do not loop waiting for data 
		if (dev_file->f_flags & O_NONBLOCK) 
		{
			spin_unlock(&dev->state_lock);
			return -EAGAIN;
		}
		
		spin_unlock(&dev->state_lock);
		
		// Sleep process again till state may have changed
		ret = wait_event_interruptible(dev->read_wq, dev->terminating|| 
						!kfifo_is_empty(&dev->fifo) );
		
		if (ret) {
			pr_info("Read interrupted by a signal\n");
			return ret;
		}		
	}
	return ret;
}


static ssize_t chrdev_blk_io_write(struct file *dev_file,
				   const char __user *usr_buf,
				   size_t data_size,
				   loff_t *offset)
{
	// this buffer will be on stack which is limited size, move to heap allocation later
	unsigned int data_copied ;
	int ret=0;
	struct chrdev_device *dev = dev_file->private_data;
	
	// Don't allow partial writes for now 
	if (data_size > FIFO_SIZE)
		return -EINVAL;
	
	for(;;) 
	{	
		spin_lock(&dev->state_lock);
		
		// Exit on terimation
		if (dev->terminating) {
			spin_unlock(&dev->state_lock);
			return -ENODEV;
		}
		
		// write if enough space available
		if (kfifo_avail(&dev->fifo) >= data_size) {
		
			// space available, copy data to kfifo
			ret = kfifo_from_user(&dev->fifo,usr_buf,data_size,&data_copied);
		
			spin_unlock(&dev->state_lock);
		
			wake_up_interruptible(&dev->read_wq);
			return ret?ret:data_copied;
		}
		
		// not enough space, non-blocking
		if (dev_file->f_flags & O_NONBLOCK) {
			spin_unlock(&dev->state_lock);
			return -EAGAIN;
		}
		
		spin_unlock(&dev->state_lock);
		
		// sleep for blocking writes
		ret = wait_event_interruptible(dev->write_wq,
						 dev->terminating ||
						(kfifo_avail(&dev->fifo) >= data_size));
		
		if(ret)
			return ret;
	}
}
	


static long chrdev_blk_ioctl(struct file *dev_file, unsigned int cmd, unsigned long arg)
{

	struct chrdev_device *dev = dev_file->private_data;
	switch(cmd)
	{
	 case CHRDEV_TERM:
	 	spin_lock(&dev->state_lock);
	 	dev->terminating = true;
	 	spin_unlock(&dev->state_lock);
	 	wake_up_all(&dev->read_wq);
	 	wake_up_all(&dev->write_wq);
	 	pr_info("Termination requested via ioctl \n");
	 	break;
	
	 default: 
		pr_err("Invalid ioctl cmd: %u\n",cmd);
		return -EINVAL;
	}
	return 0;
}

static const struct file_operations chrdev_blk_io_fops = {
	.owner  = THIS_MODULE,
	.open = chrdev_blk_io_open,
	.release = chrdev_blk_io_release ,
	.read = chrdev_blk_io_read,
	.write = chrdev_blk_io_write,
	.poll = chrdev_blk_io_poll,
	.unlocked_ioctl = chrdev_blk_ioctl,
};


/*****************************************************************/
/*******************---- MODULE INIT/EXIT---**********************/
/*****************************************************************/

static int __init chrdev_blk_init(void)
{
	int minor,success_count, ret = 0;
	
	if (max_devices > MAX_ALLOWED_DEVICES || max_devices <=0)
	{
		pr_err("Cannot load module, Max allowed devices are %d\n",MAX_ALLOWED_DEVICES);
		return -EPERM;
	}
	
	// allocate major and minor numbers
	if ( (ret = alloc_chrdev_region(&g_drv.base_dev,0,max_devices,"chrdev_io") ) ){
		pr_err("Failed to allocate device region\n");
		goto cleanup;
	}
	
	// class creation
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,0,0)
	g_drv.chdev_class = class_create(THIS_MODULE,"chrdev_io");
#else
	g_drv.chdev_class = class_create("chrdev_io");
#endif
	if (IS_ERR(g_drv.chdev_class)) {
		ret = PTR_ERR(g_drv.chdev_class);
		goto unreg_dev;
	}
	
	// create devices objects and initialize them
	g_drv.devices = kcalloc(max_devices, sizeof(struct chrdev_device),GFP_KERNEL);
	if (!g_drv.devices) {
		ret = -ENOMEM;
		goto del_class;
	}
		
	success_count =0;
	for (minor=0; minor<max_devices; minor++)
	{
		struct chrdev_device *dev = &g_drv.devices[minor];
		
		dev->device_num = g_drv.base_dev + minor;
		
		// initialize state of each device
		dev->terminating = false;
		init_waitqueue_head(&dev->read_wq);
		init_waitqueue_head(&dev->write_wq);
		init_waitqueue_head(&dev->term_wq);
		spin_lock_init(&dev->state_lock);
		atomic_set(&dev->active_refs,0);
	
		// allocate fifo space
		if ((ret = kfifo_alloc(&dev->fifo,FIFO_SIZE,GFP_KERNEL)))
			goto cleanup_devices;
		
		//cdev setup
		cdev_init(&dev->cdev,&chrdev_blk_io_fops);
		dev->cdev.owner = THIS_MODULE;
		if ( (ret = cdev_add(&dev->cdev,dev->device_num,1)) )
			goto fifo_cleanup;
			
		// create device node
		dev->ch_device = device_create(g_drv.chdev_class,
					NULL,
					dev->device_num, 
					NULL,
					"chrdev_io%d",minor);
		if(IS_ERR(dev->ch_device)) {
			ret = PTR_ERR(dev->ch_device);
			goto del_cdev;
		}
		success_count++;	
	}
	pr_info("Blocking i/o character device created successfully\n");
	return 0;


// cleanup partially initialized current device
del_cdev:
		cdev_del(&g_drv.devices[minor].cdev);	
fifo_cleanup:
		kfifo_free(&g_drv.devices[minor].fifo);
// cleanup previous fully initialized devices 
cleanup_devices:
	for (minor=0; minor<success_count; minor++) {
		device_destroy(g_drv.chdev_class, g_drv.devices[minor].device_num);
		cdev_del(&g_drv.devices[minor].cdev);
		kfifo_free(&g_drv.devices[minor].fifo);
	}
// cleanup global resources
//del_dev_data:
	kfree(g_drv.devices);
del_class:
	class_destroy(g_drv.chdev_class);
unreg_dev: 
	unregister_chrdev_region(g_drv.base_dev, max_devices);
cleanup:
	return ret;
}


static void __exit chrdev_blk_exit(void)
{
	
	int minor;
	
	// step 1: signal termination + wake all readers/writers to finish
	for (minor=0; minor< max_devices; minor++)
	{
		struct chrdev_device *dev = &g_drv.devices[minor];
		// set terminating condition to true
		spin_lock(&dev->state_lock);
		dev->terminating  = true;
		spin_unlock(&dev->state_lock);
	
		// wake up all sleeping readers to execute termination cleanup
		wake_up_all(&dev->read_wq);
		wake_up_all(&dev->write_wq);
		wait_event(dev->term_wq, atomic_read(&dev->active_refs)==0);
	}
	
	// hacky code assuming all readers exit within 10ms, fix later
	//msleep(10);
	//ret = wait_event_interruptible();
	
	// step 2: Destroy device nodes
	for (minor=0; minor<max_devices; minor++)
	{
		// tear down device file
		device_destroy(g_drv.chdev_class, g_drv.devices[minor].device_num);
		cdev_del(&g_drv.devices[minor].cdev);
		kfifo_free(&g_drv.devices[minor].fifo);
	}
		
	// step 3: global cleanup
	kfree(g_drv.devices);
	class_destroy(g_drv.chdev_class);
	
	unregister_chrdev_region(g_drv.base_dev,max_devices);
	
	pr_info("Blocking i/o character device removed successfully\n");
}


module_init(chrdev_blk_init);
module_exit(chrdev_blk_exit);



