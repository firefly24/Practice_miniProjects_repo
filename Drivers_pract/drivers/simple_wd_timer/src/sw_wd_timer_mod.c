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

static bool nowayout = false;
module_param(nowayout,bool, 0644);
MODULE_PARM_DESC(nowayout,"Watchdog cannot be terminated once it is started");

#define WD_DEFAULT_TIMEOUT_MS 5000
#define MAX_WDOGS 4
// TODO: pr_fmt customization

struct sw_wd_timer{
	struct timer_list timer_work;
	
	unsigned long timeout_jiffies;
	unsigned long last_pet_jiffies;

	// watchog state tracking	
	atomic_t ping_count;
	atomic_t armed;

	// attributes for watchdog char device file 
	dev_t device_number;
	struct device *wd_device;
	
	// attributes to enforce exclusive ownership
	spinlock_t owner_lock;
	struct file *owner;
};

static dev_t wdog_base_dev;
static struct cdev wdog_base_cdev;
static struct class *wdog_base_class;
static struct sw_wd_timer wdog_instance[MAX_WDOGS];

static void sw_wd_timer_ping(struct sw_wd_timer *swd)
{	
	if(!atomic_read(&swd->armed))
		return;	
		
	atomic_inc(&swd->ping_count);
	
	swd->last_pet_jiffies = jiffies;
	
	mod_timer(&swd->timer_work,jiffies +  swd->timeout_jiffies);
}

/*This timer callback will run in softIRQ context*/
static void sw_wd_timer_work_func(struct timer_list *t)
{
	struct sw_wd_timer *swd = container_of(t,struct sw_wd_timer,timer_work);
	
	pr_info("%s:\t SW Watchdog interrupt fired, total pings:%d, last pet time: %u, time elapsed: %u\n",
		__func__, atomic_read(&swd->ping_count),
		jiffies_to_msecs(swd->last_pet_jiffies),
		jiffies_to_msecs(jiffies - swd->last_pet_jiffies));  
	
	if (!atomic_read(&swd->armed))
		return;
	
	panic("SW watchdog expired!");
}

static void sw_wd_timer_arm(struct sw_wd_timer *swd)
{		
	// Fill out software watchdog properties
	atomic_set(&swd->ping_count,0);
	swd->last_pet_jiffies = jiffies;
	swd->timeout_jiffies = msecs_to_jiffies(WD_DEFAULT_TIMEOUT_MS);
	
	atomic_set(&swd->armed,1);
	
	mod_timer(&swd->timer_work, jiffies + swd->timeout_jiffies);	
	
	pr_info("%s:\t Watchdog timer enabled, timeout: %u\n",__func__, jiffies_to_msecs(swd->timeout_jiffies));
}

static void sw_wd_timer_disarm(struct sw_wd_timer *swd)
{
	atomic_set(&swd->armed,0);
	
	timer_delete_sync(&swd->timer_work);
	
	pr_info("%s:\t Watchdog timer disabled, timeout: %u\n",__func__, jiffies_to_msecs(swd->timeout_jiffies));
}


static int sw_wd_open(struct inode* filenode, struct file* dev_file)
{
	int minor_no = iminor(filenode);
	if (minor_no >= MAX_WDOGS)
		return -ENODEV;
	
	struct sw_wd_timer *wd = &wdog_instance[minor_no];
	
	spin_lock(&wd->owner_lock);
	
	if (wd->owner)
	{
		// already has non null owner, reject this open()
		spin_unlock(&wd->owner_lock);
		return -EBUSY;
	}
	
	wd->owner = dev_file;
	spin_unlock(&wd->owner_lock);
	
	dev_file->private_data = wd;
	sw_wd_timer_arm(wd);
	return 0;
}

static ssize_t sw_wd_write(struct file* dev_file, 
			   const char __user *user_buf,
			   size_t input_length, 
			   loff_t* offset)
{
	struct sw_wd_timer *wd = dev_file->private_data;
	
	// check for ownership
	spin_lock(&wd->owner_lock);
	if(wd->owner != dev_file)
	{
		spin_unlock(&wd->owner_lock);
		return -EPERM;
	}	
	spin_unlock(&wd->owner_lock);
	sw_wd_timer_ping(wd);
	
	// Ignoring actual payload, as right now the payload value is of no concern
	// A user process writing into the file itself indicates alive-ness, treating it as a ping
	return input_length;
}

static ssize_t sw_wd_status_read(struct file* dev_file,
				 char __user *user_buf,
				 size_t data_size,
				 loff_t* offset)
{
	struct sw_wd_timer *wd = dev_file->private_data;

	char status_buf[128];
	int len;

	// only allow first read to be successful, as cat /dev/sw_wdog will keep reading in loop and cause panic on timeout
	if (*offset !=0)
		return 0;
	
	len = scnprintf(status_buf, sizeof(status_buf)," Owned: %s, Armed: %d, Ping count: %d, Last pet time: %u\n",
				 (wd->owner==NULL)?"No":"Yes",
				 atomic_read(&wd->armed),
				 atomic_read(&wd->ping_count),
				 jiffies_to_msecs(wd->last_pet_jiffies) );
				 
	if (copy_to_user(user_buf, status_buf, len ))
		return -EFAULT;	
	
	*offset += len;
	return len;
}
				

static int sw_wd_release(struct inode* filenode, struct file* dev_file)
{
	struct sw_wd_timer *wd = dev_file->private_data;

	spin_lock(&wd->owner_lock);
	if(wd->owner != dev_file)
	{
		// owner is different, reject release
		spin_unlock(&wd->owner_lock);
		return 0;
	}
	//!release ownership
	if (!nowayout)
		wd->owner = NULL;
	spin_unlock(&wd->owner_lock);	
	
	// does nowayout affect module unload? How about the user prog comes to end successfully
	if (!nowayout)
		sw_wd_timer_disarm(wd);
	return 0;
}

static const struct file_operations sw_wd_fops =  {
	.owner = THIS_MODULE ,
	.open = sw_wd_open,
	.write = sw_wd_write,
	.read = sw_wd_status_read,
	.release = sw_wd_release, 
};

static int __init sw_wd_timer_init(void)
{
	int ret=0;
	
	// create a device number for our character device
	if( (ret = alloc_chrdev_region(&wdog_base_dev,0,MAX_WDOGS, "simple_wd_timer") ) )
		return ret; 
		
	for(int minor=0;minor<MAX_WDOGS;minor++)
	{
		struct sw_wd_timer *wd = &wdog_instance[minor];
		// setup ownership attributes
		wd->owner = NULL;
		spin_lock_init(&wd->owner_lock);
	
		// setup timer first
		timer_setup(&wd->timer_work, sw_wd_timer_work_func,0);
		atomic_set(&wd->armed,0);
		atomic_set(&wd->ping_count,0);
	}
	
	// initialize a cdev object for char device registration with vfs	
	cdev_init(&wdog_base_cdev,&sw_wd_fops);
	wdog_base_cdev.owner = THIS_MODULE;
	
	// add char device to system to make it live
	if ((ret = cdev_add(&wdog_base_cdev,wdog_base_dev,MAX_WDOGS)) )
		goto unreg_chrdev;
	
	// create device class under /sys/class 
	wdog_base_class = class_create("simple_wd_timer");
	if (IS_ERR(wdog_base_class))
	{
		ret = PTR_ERR(wdog_base_class);
		goto del_cdev;
	}
	
	int success_device_count = 0;
	//create device files under this class
	for (int minor = 0; minor < MAX_WDOGS; minor++)
	{
		struct sw_wd_timer *wd = &wdog_instance[minor];
		
		wd->device_number = MKDEV(MAJOR(wdog_base_dev),minor);
		
		wd->wd_device = device_create(wdog_base_class,NULL,wd->device_number,NULL,"sw_wdog%d",minor);
		
		
		if (IS_ERR(wd->wd_device))
		{
			ret = PTR_ERR(wd->wd_device);
			goto del_device;
		}
		success_device_count++;
		
		pr_info("%s:\t Device number(<major>:<minor>): [%d:%d] \n",__func__,
								 MAJOR(wd->device_number),
								 MINOR(wd->device_number) );
	}
	
	pr_info("%s:\t Software watchdog module loaded succesfully\n",__func__);
								 
	return ret;

del_device:
	for(int i=0;i<success_device_count;i++)	
		device_destroy(wdog_base_class, wdog_instance[i].device_number);	
	class_destroy(wdog_base_class);
	
del_cdev:
	cdev_del(&wdog_base_cdev);

unreg_chrdev:
	for (int i=0;i<success_device_count;i++)
		timer_delete_sync(&wdog_instance[i].timer_work);
	unregister_chrdev_region(wdog_base_dev,MAX_WDOGS);
	
	return ret;
}

static void __exit sw_wd_timer_exit(void)
{	
	
	for (int minor = 0;minor<MAX_WDOGS; minor++)
		device_destroy(wdog_base_class, wdog_instance[minor].device_number);
	
	class_destroy(wdog_base_class);
	
	cdev_del(&wdog_base_cdev);
	
	for (int minor = 0;minor<MAX_WDOGS; minor++)
		sw_wd_timer_disarm(&wdog_instance[minor]);
	
	unregister_chrdev_region(wdog_base_dev,MAX_WDOGS);
	
	// before timer teardown, make sure char device is destroyed so that userspace cannot issue new pings during timer teardown

	pr_info("%s:\t Successfully removed sw watchdog module\n",__func__);
}


module_init(sw_wd_timer_init);
module_exit(sw_wd_timer_exit);

