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


MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernel module demonstrating blocking and non-blocking i/o");
MODULE_AUTHOR("Firefly24");

#define FIFO_SIZE 1024


struct chrdev_state{
	//bool data_available;
	bool terminating;
	
	wait_queue_head_t wq;
	spinlock_t state_lock;
	
	dev_t device_num;
	struct cdev cdev;
	struct class *chdev_class;
	struct device *ch_device;
	
	struct kfifo fifo;
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

static __poll_t chrdev_blk_io_poll(struct file *dev_file, struct poll_table_struct *polltable)
{
	__poll_t status =0;
	
	// register waitqueue for sleeping
	poll_wait(dev_file,&dev.wq,polltable);
	
	// check state
	spin_lock(&dev.state_lock);
	
	if (dev.terminating)
		status |= POLLERR;
		
	if (!kfifo_is_empty(&dev.fifo))
		status |= POLLIN | POLLRDNORM;  // check poll() syscall manpage for these bits
	
	spin_unlock(&dev.state_lock);
	
	return status;

}

static ssize_t chrdev_blk_io_read(struct file *dev_file, 
				  char __user *usr_buf, 
				  size_t data_size, 
				  loff_t *offset)
{
	int ret = 0;
	// this buffer will be on stack which is limited size, move to heap allocation later
	char buf[FIFO_SIZE];

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
		if (!kfifo_is_empty(&dev.fifo))
		{
			//dev.data_available = false;
			
			size_t data_avail = min(data_size,(size_t)kfifo_len(&dev.fifo));
			ret = kfifo_out(&dev.fifo,buf,data_avail);
			
			spin_unlock(&dev.state_lock);
			
			// Do the data transfer here
			if (copy_to_user(usr_buf,buf,data_avail)) {
				pr_warn("Copy to user failed \n");
				return -EFAULT;
			}
			
			return data_avail;
		}
		
		// For non-blocking I/O do not loop waiting for data 
		if (dev_file->f_flags & O_NONBLOCK)
		{
			spin_unlock(&dev.state_lock);
			return -EAGAIN;
		}
		
		spin_unlock(&dev.state_lock);
		
		// Sleep process again till state may have changed
		ret = wait_event_interruptible(dev.wq, dev.terminating|| !kfifo_is_empty(&dev.fifo) );
		
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
	// this buffer will be on stack which is limited size, move to heap allocation later
	char buf[FIFO_SIZE];
	//int len =0;
	
	if (data_size > FIFO_SIZE)
		return -EINVAL;
		
	spin_lock(&dev.state_lock);
	
	// don't write if device in terminating state
	if (dev.terminating) {
		spin_unlock(&dev.state_lock);
		return -ENODEV;
	}
	
	// reject write if data is larger than available space on kfifo
	if (kfifo_avail(&dev.fifo) < data_size) {
		spin_unlock(&dev.state_lock);
		return -EAGAIN;
	}
	
	spin_unlock(&dev.state_lock);
	
	// data available, write data to kernel buffer here
	if ( copy_from_user(buf,usr_buf,data_size) ) {
		pr_warn("Content copy from user buffer failed\n");
		return -EFAULT;
	}
	
	//copy data to kfifo
	// note: we're doing double copy, copy from user buf to kernel buf, then copy data to kfifo, can this be improved?
	spin_lock(&dev.state_lock);
	//dev.data_available = true;
	if (kfifo_avail(&dev.fifo) < data_size)
	{
		spin_unlock(&dev.state_lock);
		return -EAGAIN;
	}
	size_t data_copied = kfifo_in(&dev.fifo,buf,data_size);
	spin_unlock(&dev.state_lock);
	
	wake_up_interruptible(&dev.wq);
	
	return data_copied;
}

static const struct file_operations chrdev_blk_io_fops = {
	.owner  = THIS_MODULE,
	.open = chrdev_blk_io_open,
	.release = chrdev_blk_io_release ,
	.read = chrdev_blk_io_read,
	.write = chrdev_blk_io_write,
	.poll = chrdev_blk_io_poll,
};


/*----------- Module init/exit-----------------------------------*/

static int __init chrdev_blk_init(void)
{
	int ret = 0;
	
	//dev.data_available = false;
	dev.terminating = false;
	
	
	init_waitqueue_head(&dev.wq);
	spin_lock_init(&dev.state_lock);
	
	if ((ret = kfifo_alloc(&dev.fifo,FIFO_SIZE,GFP_KERNEL)))
		return ret;
	
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
	kfifo_free(&dev.fifo);
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
	
	kfifo_free(&dev.fifo);
}


module_init(chrdev_blk_init);
module_exit(chrdev_blk_exit);



