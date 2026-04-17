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
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/version.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Darshana");
MODULE_DESCRIPTION("Simple watchdog timer practice");

static bool nowayout = false;
module_param(nowayout,bool, 0644);
MODULE_PARM_DESC(nowayout,"Watchdog cannot be terminated once it is started");

#define WD_DEFAULT_TIMEOUT_MS 5000
#define WD_PANIC_DELAY_DEFAULT_MS 2000
#define MAX_WDOGS 4

enum wd_recovery_policy{
	WD_POLICY_PANIC=0,
	WD_POLICY_LOG_ONLY,
	WD_POLICY_DELAYED_PANIC,
	WD_POLICY_MAX,
};

struct wd_status{
	bool is_owned;
	bool is_armed;
	int timeout_ms;
	int ping_count;
	unsigned long time_since_last_pet_ms;
};


// ioctl related definitions
#define WD_MAGIC 'W'
#define WD_ARM _IO(WD_MAGIC,1)
#define WD_DISARM _IO(WD_MAGIC,2)
#define WD_SET_TIMEOUT _IOW(WD_MAGIC,3,int)
#define WD_GET_STATUS _IOR(WD_MAGIC,4,struct wd_status)
#define WD_SET_RECOVERY_POLICY _IOW(WD_MAGIC,5,enum wd_recovery_policy)
#define WD_SET_PANIC_DELAY_MS _IOW(WD_MAGIC,6,int)


// TODO: pr_fmt customization

struct sw_wd_timer{
	struct timer_list detect_expiry;
	
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
	
	// RECOVERY policy related
	enum wd_recovery_policy recovery_policy;
	struct delayed_work panic_work;
	unsigned int panic_delay_ms;
	struct work_struct recovery_work;
	
	atomic_t expired;
	wait_queue_head_t wq;
};


static dev_t wdog_base_dev;
static struct cdev wdog_base_cdev;
static struct class *wdog_base_class;
static struct sw_wd_timer wdog_instance[MAX_WDOGS];



/*******************************************************************************************************/
/*******************---WATCHDOG HELPER FUNCTIONS----****************************************************/
/*******************************************************************************************************/

static void sw_wd_timer_trigger_panic(struct work_struct* work)
{
	panic("SW watchdog expired!");
}

/*This timer callback will run in softIRQ context*/
static void sw_wd_timer_handle_expire(struct timer_list *t)
{
	struct sw_wd_timer *swd;
	swd = container_of(t,struct sw_wd_timer,detect_expiry);
	
	// Don't trigger expiry if already disarmed
	if (!atomic_read(&swd->armed))
		return;
	
	// Don't trigger recovery again if already expired
	if (atomic_xchg(&swd->expired,1))
		return;

	atomic_set(&swd->armed,0);
	
	wake_up_interruptible(&swd->wq);
	pr_warn("SW watchdog expired!\n");
	schedule_work(&(swd->recovery_work));
}

static void sw_wd_timer_handle_recovery(struct work_struct *work)
{
	struct sw_wd_timer *swd = container_of(work,struct sw_wd_timer, recovery_work);
	
	// Do not trigger recovery if watchdog is already rearmed for next use
	if (!atomic_read(&swd->expired))
		return;
	
	pr_info("%s:\t SW Watchdog interrupt fired, total pings:%d, last pet time: %u, time elapsed: %u\n",
		__func__, atomic_read(&swd->ping_count),
		jiffies_to_msecs(swd->last_pet_jiffies),
		jiffies_to_msecs(jiffies - swd->last_pet_jiffies));  
	
	switch(swd->recovery_policy)
	{
	case WD_POLICY_LOG_ONLY:
		break;
	case WD_POLICY_DELAYED_PANIC:
		schedule_delayed_work(&swd->panic_work,msecs_to_jiffies(swd->panic_delay_ms));
		break;
	case WD_POLICY_PANIC:
		schedule_delayed_work(&swd->panic_work,0);
		break;
	default:
		break;
	}
}

static int sw_wd_timer_ping(struct sw_wd_timer *swd)
{	
	if(!atomic_read(&swd->armed))
	{
		pr_warn("Watchdog pinged while disarmed! \n");
		return -EINVAL;	
	}
	/*
	if (atomic_read(&swd->expired))
	{
		pr_warn("Watchdog pinged while expired! \n");
		return -EIO;	
	}
	*/	
	atomic_inc(&swd->ping_count);
	WRITE_ONCE(swd->last_pet_jiffies,jiffies);
	mod_timer(&swd->detect_expiry,jiffies +  swd->timeout_jiffies);
	
	return 0;
}

static void sw_wd_reset_lifecycle(struct sw_wd_timer *swd)
{
	//cancel pending work
	timer_delete_sync(&swd->detect_expiry);
	cancel_work_sync(&swd->recovery_work);
	cancel_delayed_work_sync(&swd->panic_work);
	
	//reset stats
	atomic_set(&swd->ping_count,0);
	WRITE_ONCE(swd->last_pet_jiffies,jiffies);
	atomic_set(&swd->expired,0);
}

static void sw_wd_timer_arm(struct sw_wd_timer *swd)
{		
	// reset wdog lifecycle 
	sw_wd_reset_lifecycle(swd);
	
	// arm wdog after resetting lifecycle and set timer
	atomic_set(&swd->armed,1);
	mod_timer(&swd->detect_expiry, jiffies + swd->timeout_jiffies);	
	
	pr_info("%s:\t Watchdog timer enabled, timeout: %u\n",__func__, jiffies_to_msecs(swd->timeout_jiffies));
}

static void sw_wd_timer_disarm(struct sw_wd_timer *swd)
{
	// TODO: since we're moving arm/disarm logic to ioctl, handle nowayout semantics here too
	
	if (nowayout)
		return;
	
	atomic_set(&swd->armed,0);
	
	timer_delete_sync(&swd->detect_expiry);
	cancel_work_sync(&swd->recovery_work);
	cancel_delayed_work_sync(&swd->panic_work);
	
	pr_info("%s:\t Watchdog timer disabled, timeout: %u\n",__func__, jiffies_to_msecs(swd->timeout_jiffies));
}



/*******************************************************************************************************/
/*******************----WATCHDOG DEVICE FILE OPS----****************************************************/
/*******************************************************************************************************/

static int sw_wd_open(struct inode* filenode, struct file* dev_file)
{
	struct sw_wd_timer *wd;
	int minor_no = iminor(filenode);
	if (minor_no >= MAX_WDOGS)
		return -ENODEV;
	
	wd = &wdog_instance[minor_no];
	
	spin_lock(&wd->owner_lock);
	
	if (wd->owner)
	{
		// already has non null owner, reject this open()
		spin_unlock(&wd->owner_lock);
		return -EBUSY;
	}
	
	wd->owner = dev_file;
	dev_file->private_data = wd;
	spin_unlock(&wd->owner_lock);	
	

	return 0;
}

static ssize_t sw_wd_write(struct file* dev_file, 
			   const char __user *user_buf,
			   size_t input_length, 
			   loff_t* offset)
{
	bool is_owner;
	int ret;
	struct sw_wd_timer *wd = dev_file->private_data;
	
	// check for ownership
	spin_lock(&wd->owner_lock);
	is_owner  = (wd->owner == dev_file);
	spin_unlock(&wd->owner_lock);
	
	if(!is_owner)
		return -EPERM;
	
	ret = 0;	
	if ((ret = sw_wd_timer_ping(wd)))
		return ret;
	
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
	int len,ret;
	bool is_owner;
	
	// only allow first read to be successful, as cat /dev/sw_wdog will keep reading in loop and cause panic on timeout
	if (*offset !=0)
		return 0;
		
	spin_lock(&wd->owner_lock);
	is_owner = (wd->owner == dev_file);
	spin_unlock(&wd->owner_lock);
	
	if (!is_owner)
		return -EPERM;
	
	if (!atomic_read(&wd->expired)){
		ret = wait_event_interruptible(wd->wq,atomic_read(&wd->expired));
		if (ret)
		{
			pr_warn("Read  interrupted by a signal \n");
			return ret;
		}
	}
	
	len = scnprintf(status_buf, sizeof(status_buf)," Owned caller: %s, Armed: %d, Ping count: %d, Last pet time: %u, Expired: %d\n",
				 (!is_owner)?"No":"Yes",
				 atomic_read(&wd->armed),
				 atomic_read(&wd->ping_count),
				 jiffies_to_msecs(wd->last_pet_jiffies),
				 atomic_read(&wd->expired) );
				 
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
	
	/* does nowayout affect module unload? How about the user prog comes to end successfully
	TODO: remove disarm here and only handle through ioctl ? 
	if (!nowayout)
		sw_wd_timer_disarm(wd);
	*/
	return 0;
}

static long sw_wd_ioctl(struct file *dev_file, unsigned int cmd, unsigned long arg)
{
	bool is_permitted_owner;
	int timeout,delay_time;
	struct sw_wd_timer *wd  = dev_file->private_data;
	
	spin_lock(&wd->owner_lock);
	is_permitted_owner = ((wd->owner == dev_file) || (cmd == WD_GET_STATUS));
	spin_unlock(&wd->owner_lock);
	
	if (!is_permitted_owner)
		return -EPERM;
	
	switch(cmd)
	{
	case WD_ARM:
		if (atomic_read(&wd->armed) && !atomic_read(&wd->expired))
			return -EBUSY;
		sw_wd_timer_arm(wd);
		break;
	case WD_DISARM:
		if (!nowayout)
			sw_wd_timer_disarm(wd);
		break;
	case WD_SET_TIMEOUT:
	{
		if (copy_from_user(&timeout,(int __user *)arg,sizeof(timeout)) )
			return -EFAULT;
			
		timeout = (timeout<=0)?WD_DEFAULT_TIMEOUT_MS:timeout;
		WRITE_ONCE(wd->timeout_jiffies,msecs_to_jiffies(timeout));
		
		break;
	}
	case WD_GET_STATUS:
	{
		struct wd_status status;
		status.is_armed = atomic_read(&wd->armed);
		status.is_owned = (wd->owner != NULL);
		status.ping_count = atomic_read(&wd->ping_count);
		status.timeout_ms = jiffies_to_msecs(READ_ONCE(wd->timeout_jiffies));
		status.time_since_last_pet_ms = (!status.is_armed)?0
						:jiffies_to_msecs(jiffies - READ_ONCE(wd->last_pet_jiffies) );
		
		if (copy_to_user((struct wd_status __user*)arg,&status, sizeof(struct wd_status)) )
			return -EFAULT;
		
		break;
	}
	case WD_SET_RECOVERY_POLICY:
	{
		enum wd_recovery_policy policy;
		if (atomic_read(&wd->armed))
			return -EBUSY;
		
		if (copy_from_user(&policy,(int __user *)arg,sizeof(policy)) )
			return -EFAULT; 
		if (policy <0 || policy >= WD_POLICY_MAX)
			return -EINVAL;
		wd->recovery_policy  = policy;
		break;
	}
	case WD_SET_PANIC_DELAY_MS:
	{
		if (atomic_read(&wd->armed))
			return -EBUSY;
		
		if (copy_from_user(&delay_time, (int __user *)arg, sizeof(delay_time)))
			return -EFAULT;
		wd->panic_delay_ms = delay_time;
		break;
	}
	default:
		return -EINVAL;
	}
	
	return 0;
}

static const struct file_operations sw_wd_fops =  {
	.owner = THIS_MODULE ,
	.open = sw_wd_open,
	.write = sw_wd_write,
	.read = sw_wd_status_read,
	.release = sw_wd_release,
	.unlocked_ioctl = sw_wd_ioctl,
};



/*******************************************************************************************************/
/*******************----WATCHDOG MODULE INIT/EXIT---****************************************************/
/*******************************************************************************************************/

static int __init sw_wd_timer_init(void)
{
	int minor, success_device_count,ret=0;
	
	// create a device number for our character device
	if( (ret = alloc_chrdev_region(&wdog_base_dev,0,MAX_WDOGS, "simple_wd_timer") ) )
		return ret; 
		
	for(minor=0; minor<MAX_WDOGS; minor++)
	{
		struct sw_wd_timer *wd = &wdog_instance[minor];
		// setup ownership attributes
		wd->owner = NULL;
		spin_lock_init(&wd->owner_lock);
		init_waitqueue_head(&wd->wq);
	
		// setup timer first
		wd->timeout_jiffies = msecs_to_jiffies(WD_DEFAULT_TIMEOUT_MS);
		WRITE_ONCE(wd->last_pet_jiffies,jiffies);
		timer_setup(&wd->detect_expiry, sw_wd_timer_handle_expire,0);
		atomic_set(&wd->armed,0);
		atomic_set(&wd->ping_count,0);
		atomic_set(&wd->expired,0);
		
		wd->panic_delay_ms = WD_PANIC_DELAY_DEFAULT_MS;
		wd->recovery_policy = WD_POLICY_LOG_ONLY;
		INIT_WORK(&(wd->recovery_work), sw_wd_timer_handle_recovery );
		INIT_DELAYED_WORK(&(wd->panic_work),sw_wd_timer_trigger_panic);
	}
	
	// initialize a cdev object for char device registration with vfs	
	cdev_init(&wdog_base_cdev,&sw_wd_fops);
	wdog_base_cdev.owner = THIS_MODULE;
	
	// add char device to system to make it live
	if ((ret = cdev_add(&wdog_base_cdev,wdog_base_dev,MAX_WDOGS)) )
		goto unreg_chrdev;
	
	// create device class under /sys/class
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,0,0) 
	wdog_base_class = class_create(THIS_MODULE,"simple_wd_timer");
#else
	wdog_base_class = class_create("simple_wd_timer");
#endif
	if (IS_ERR(wdog_base_class))
	{
		ret = PTR_ERR(wdog_base_class);
		goto del_cdev;
	}
	
	success_device_count = 0;
	//create device files under this class
	for (minor = 0; minor < MAX_WDOGS; minor++)
	{
		struct sw_wd_timer *wd = &wdog_instance[minor];
		
		wd->device_number = MKDEV(MAJOR(wdog_base_dev),minor);
		
		wd->wd_device = device_create(wdog_base_class,NULL,wd->device_number,NULL,"sw_wdog%d",minor);
		
		
		if (IS_ERR(wd->wd_device))
		{
			ret = PTR_ERR(wd->wd_device);
			pr_err("%s:\t Failed to create device for minor no.: %d",__func__, minor);
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
	for(minor=0; minor<success_device_count; minor++)	
		device_destroy(wdog_base_class, wdog_instance[minor].device_number);	
	class_destroy(wdog_base_class);
	
del_cdev:
	cdev_del(&wdog_base_cdev);

unreg_chrdev:
	for (minor=0; minor<MAX_WDOGS; minor++)
	{
		timer_delete_sync(&wdog_instance[minor].detect_expiry);
		cancel_work_sync(&wdog_instance[minor].recovery_work);
		cancel_delayed_work_sync(&wdog_instance[minor].panic_work);
	}
	unregister_chrdev_region(wdog_base_dev,MAX_WDOGS);
	
	return ret;
}

static void __exit sw_wd_timer_exit(void)
{	
	int minor;
	for (minor=0; minor<MAX_WDOGS; minor++)
	{
		sw_wd_timer_disarm(&wdog_instance[minor]);
		device_destroy(wdog_base_class, wdog_instance[minor].device_number);
	}
	
	class_destroy(wdog_base_class);
	
	cdev_del(&wdog_base_cdev);
		
	unregister_chrdev_region(wdog_base_dev,MAX_WDOGS);
	
	// before timer teardown, make sure char device is destroyed so that userspace cannot issue new pings during timer teardown

	pr_info("%s:\t Successfully removed sw watchdog module\n",__func__);
}


module_init(sw_wd_timer_init);
module_exit(sw_wd_timer_exit);

