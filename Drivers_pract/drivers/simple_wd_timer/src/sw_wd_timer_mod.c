#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/kdev_t.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Darshana");
MODULE_DESCRIPTION("Simple watchdog timer practice");

#define WD_DEFAULT_TIMEOUT_MS 5000

// TODO: pr_fmt customization

struct sw_wd_timer{

	//struct device *dev;

	struct timer_list timer_work;
	
	unsigned long timeout_jiffies;
	unsigned long last_pet_jiffies;

	atomic_t ping_count;
	atomic_t is_active;

	dev_t device_number;
	struct cdev sw_wd_cdev;
	struct class *sw_wd_class;
	struct device* wd_device;
	
};

// writing the module for one instance only for now
static struct sw_wd_timer wd;

static void sw_wd_timer_ping(struct sw_wd_timer *swd)
{
	if (IS_ERR_OR_NULL(swd))
		return;
	
	if(!atomic_read(&swd->is_active))
		return;	
		
	atomic_add(1,&swd->ping_count);
	swd->last_pet_jiffies = jiffies;
	
	mod_timer(&swd->timer_work,jiffies +  swd->timeout_jiffies);
}

static void sw_wd_timer_work_func(struct timer_list *t)
{
	struct sw_wd_timer *swd = container_of(t,struct sw_wd_timer,timer_work);
	
	pr_info("%s:\t SW Watchdog interrupt fired, last pet time: %lu, time elapsed: %u\n",
		__func__,
		swd->last_pet_jiffies,
		jiffies_to_msecs(jiffies - swd->last_pet_jiffies));  
	
	if (!atomic_read(&swd->is_active))
		return;
	
	panic("SW watchdog expired!");
}

static void sw_wd_timer_start(struct sw_wd_timer *swd)
{
	if(IS_ERR_OR_NULL(swd))
		return;
	
	// Fill out software watchdog properties
	atomic_set(&swd->ping_count,0);
	swd->last_pet_jiffies = jiffies;
	swd->timeout_jiffies = msecs_to_jiffies(WD_DEFAULT_TIMEOUT_MS);

	timer_setup(&swd->timer_work, sw_wd_timer_work_func,0);
	
	atomic_set(&swd->is_active,1);
	
	mod_timer(&swd->timer_work, jiffies + swd->timeout_jiffies);	
	
	pr_info("%s:\t Watchdog timer enabled, timeout: %lu\n",__func__, swd->timeout_jiffies);
}

static void sw_wd_timer_stop(struct sw_wd_timer *swd)
{
	atomic_set(&swd->is_active,0);
	
	timer_delete_sync(&swd->timer_work);
	
	pr_info("%s:\t Watchdog timer disabled, timeout: %lu\n",__func__, swd->timeout_jiffies);
}


static int sw_wd_open(struct inode* filenode, struct file* dev_file)
{
	sw_wd_timer_start(&wd);
	return 0;
}

static ssize_t sw_wd_write(struct file* dev_file, 
			   const char __user *user_buf,
			   size_t input_length, 
			   loff_t* offset)
{
	sw_wd_timer_ping(&wd);
	return input_length;
}

static int sw_wd_release(struct inode* filenode, struct file* dev_file)
{
	sw_wd_timer_stop(&wd);
	return 0;
}

static const struct file_operations sw_wd_fops =  {
	.owner = THIS_MODULE ,
	.open = sw_wd_open,
	.write = sw_wd_write,
	.release = sw_wd_release, 
};



static int __init sw_wd_timer_init(void)
{
	int ret=0;
	
	// create a device number for our character device
	if( (ret = alloc_chrdev_region(&wd.device_number,0,1, "simple_wd_timer") ) )
		return ret;
	
	// initialize a cdev object for char device registration with vfs	
	cdev_init(&wd.sw_wd_cdev,&sw_wd_fops);
	wd.sw_wd_cdev.owner = THIS_MODULE;
	
	// add char device to system to make it live
	if ((ret = cdev_add(&wd.sw_wd_cdev,wd.device_number,1)) )
	{
		unregister_chrdev_region(wd.device_number,1);
		return ret;
	}
	
	//create device files
	
	// create device class under /sys/class 
	wd.sw_wd_class = class_create("simple_wd_timer");
	if (IS_ERR_OR_NULL(wd.sw_wd_class))
	{
		// return error
	}
	
	//create device file under this class
	wd.wd_device = device_create(wd.sw_wd_class,NULL,wd.device_number,NULL,"sw_wdog");
	
	pr_info("%s:\t Software watchdog module loaded succesfully\n",__func__);
	pr_info("%s:\t Device number(<major>:<minor>): [%d:%d] \n",__func__,
								 MAJOR(wd.device_number),
								 MINOR(wd.device_number) );
	
	return ret;
}

static void __exit sw_wd_timer_exit(void)
{	
	sw_wd_timer_stop(&wd);

	timer_shutdown_sync(&wd.timer_work);
	
	device_destroy(wd.sw_wd_class, wd.device_number);
	
	class_destroy(wd.sw_wd_class);
	
	cdev_del(&wd.sw_wd_cdev);
	
	unregister_chrdev_region(wd.device_number,1);

	pr_info("%s:\t Successfully removed sw watchdog module\n",__func__);
}


module_init(sw_wd_timer_init);
module_exit(sw_wd_timer_exit);









