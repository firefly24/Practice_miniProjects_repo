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
	bool condition;
	bool terminating;
	
	wait_queue_head_t wq;
	spinlock_t state_lock;
	
	dev_t device_num;
	struct cdev cdev;
	struct class *chdev_class;
	struct device *ch_device;
};


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
	return data_size;
}


static ssize_t chrdev_blk_io_write(struct file *dev_file,
				   const char __user *usr_buf,
				   size_t data_size,
				   loff_t *offset)
{
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
	
	return ret;
}


static void __exit chrdev_blk_exit(void)
{

}


module_init(chrdev_blk_init);
module_exit(chrdev_blk_exit);



