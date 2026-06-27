#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include "telemetry_ring.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("firelfy24");
MODULE_DESCRIPTION("Simple telemetry module for practice");

#define PRODUCER_SLEEP_MS 1000

struct telemetry_dev{
	
	// device related
	dev_t device_num;
	struct cdev cdev;
	struct class *telem_class;
	struct device *device;
	
	// buffer for data transfer
	struct telemetry_ring_buf buf;
	
	/* buffer activity notification for producers/consumers */
	wait_queue_head_t has_data_wq;
	wait_queue_head_t has_space_wq;
	
	// producer related
	struct task_struct *producer_thread;
	

	// Ownership and lifecycle tracking
	bool shutdown_session;
	bool has_owner;
	spinlock_t ownership_lock;
	
	// device info
	uint64_t seq_no;
};

struct telemetry_dev *tdev;


/*****************************************************************************/
 
static int producer_thread_fn(void *data)
{
	struct telemetry_dev *tdev = data;
	//struct telemetry_record *record;
	struct telemetry_record record;
	int ret=0;
	
	if(!data)
		return -EINVAL;
		
	if (tdev->buf.records == NULL)
		return -EINVAL;
	
	printk(KERN_INFO "Starting Producer thread\n");
	
	while(!kthread_should_stop())
	{
		// generate record
		record.seq_no = tdev->seq_no++;
		record.timestamp_ms = ktime_to_ms(ktime_get());
		record.value = 42;
		
		ret = wait_event_interruptible(tdev->has_space_wq,
					!ring_full(&tdev->buf) || READ_ONCE(tdev->shutdown_session));
		
		// interuupted by a signal
		if(ret)
			return ret;
			
		if(READ_ONCE(tdev->shutdown_session))
			return 0;
		
		// push to ring buffer
		if ( ring_push(&tdev->buf,&record) == 0 )
		{
			wake_up_interruptible(&tdev->has_data_wq);
			printk(KERN_INFO "Successfully pushed: %llu\n",record.seq_no );	
		}
		else
		{
			printk(KERN_INFO "Failed to push%llu\n",record.seq_no);
		}
		
		//sleep 
		msleep(PRODUCER_SLEEP_MS);
	}
	
	printk(KERN_INFO "Stopping producer thread\n");
	
	return ret;
}

int producer_thread_set_and_run(struct telemetry_dev *tdev)
{
	int ret= 0;
	
	if (tdev->producer_thread)
		return -EBUSY;
	
	/* Create and initialize kthread */
	tdev->producer_thread = kthread_create(producer_thread_fn,(void *) tdev,"Producer_telemetry");
	
	if(IS_ERR(tdev->producer_thread))
	{
		ret = PTR_ERR(tdev->producer_thread);
		tdev->producer_thread = NULL;
		return ret ;
	}
	
	/* run the producer thread */
	wake_up_process(tdev->producer_thread);
	
	return 0;
}


/*****************************************************************************/

static int telemetry_open(struct inode *filenode, struct file *dev_file)
{
	/* Setup and run producer thread here */
	int ret = 0;
	struct telemetry_dev *tdev = container_of(filenode->i_cdev,
						struct telemetry_dev,cdev);
	
	/* Take ownership if device not already owned/opened*/			
	spin_lock(&tdev->ownership_lock);		
	if (tdev->has_owner)
	{
		spin_unlock(&tdev->ownership_lock);
		printk(KERN_INFO "Device is already opened by another process\n");
		return -EBUSY;
	}
	
	dev_file->private_data = tdev;
	tdev->has_owner = true;
	
	spin_unlock(&tdev->ownership_lock);
	
	WRITE_ONCE(tdev->shutdown_session,false);
	
	printk(KERN_INFO "Open: Acquiring telemetry device ownership\n");
	
	/* Run the producer thread after taking ownership*/
	if ( (ret = producer_thread_set_and_run(tdev)) )
	{
		spin_lock(&tdev->ownership_lock);
		tdev->has_owner = false;
		spin_unlock(&tdev->ownership_lock);
	}
	
	return ret;
}

static int telemetry_release(struct inode *filenode, struct file *dev_file)
{
	struct telemetry_dev *tdev = dev_file->private_data;
	
	printk(KERN_INFO "Releasing telemetry device ownership\n");
	
	WRITE_ONCE(tdev->shutdown_session,true);
	wake_up_all(&tdev->has_data_wq);
	wake_up_all(&tdev->has_space_wq);
	
	/* stop producer thread and complete work before releasing ownership*/
	if(tdev->producer_thread != NULL)
	{
		kthread_stop(tdev->producer_thread);
		
		tdev->producer_thread = NULL;
	}
	
	ring_reset(&tdev->buf);
	//WRITE_ONCE(tdev->shutdown_session,false);
	
	/* release ownership of device */
	spin_lock(&tdev->ownership_lock);
	tdev->has_owner = false;
	spin_unlock(&tdev->ownership_lock);
	
	return 0;
}

static ssize_t telemetry_read(struct file *dev_file,
				char __user *user_buf,
				size_t data_size,
				loff_t *offset)
{

	struct telemetry_dev *tdev = dev_file->private_data;
	
	size_t idx=0;
	int ret =0;	
	ssize_t records_to_return =0;
	ssize_t bytes_to_copy =0;
	struct telemetry_record *records;
	ssize_t records_requested = data_size/ sizeof(struct telemetry_record);
	
	// This read version will only work for single producer single consumer model
	
	if(records_requested <=0)
		return -EINVAL;
		
	// Block until data is available in the ring
	ret = wait_event_interruptible(tdev->has_data_wq, 
					!ring_empty(&tdev->buf) || READ_ONCE(tdev->shutdown_session));
	
	// Read interrupted by a signal
	if(ret)
		return ret; 
		
	if(READ_ONCE(tdev->shutdown_session))
		return 0;
	
	records_to_return = min_t(ssize_t,records_available(&tdev->buf), records_requested);
	bytes_to_copy = records_to_return*sizeof(struct telemetry_record);
	
	if (records_to_return <= 0)
		return -ENODATA;
	
	records = kcalloc(records_to_return,sizeof(struct telemetry_record), GFP_KERNEL);
	if (!records)
		return -ENOMEM;

	// TODO: How to avoid multiple copies of record data here
		
	while(idx< records_to_return)
	{
		ring_pop(&tdev->buf,&records[idx]); 
		idx++;
	}
	
	wake_up_interruptible(&tdev->has_space_wq);
	
	// Since telemetry buffer is ring buffer, we cannot guarantee contiguous data in memory, so using a temporary buffer to copy availble data first
	ret = copy_to_user(user_buf,records,bytes_to_copy);
	if(ret)
	{
		ret = -EFAULT;
		goto free_records;
	}
	ret = bytes_to_copy;
	
free_records:
	kfree(records);
	
	
	return ret;
}



static const struct file_operations telemetry_drv_fops = {
	.owner = THIS_MODULE,
	.open = telemetry_open,
	.release = telemetry_release,
	.read = telemetry_read,
};

 /*****************************************************************************/


static int __init telemetry_dev_init(void)
{
	int ret=0;
	uint32_t capacity = 5;
	
	tdev = kzalloc(sizeof(struct telemetry_dev),GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;
	
	printk(KERN_INFO "Telemetry_init\n");
		
	/* Create and initialize ring buffer for telemetry records */
	if ((ret = ring_init(&tdev->buf, capacity)) )
		goto free_dev;
	
	/* zero initialize values needed in open() call */
	tdev->producer_thread = NULL;
	tdev->has_owner = false;
	spin_lock_init(&tdev->ownership_lock);
	tdev->seq_no = 0;
	init_waitqueue_head(&tdev->has_data_wq);
	init_waitqueue_head(&tdev->has_space_wq);
	tdev->shutdown_session = false;
		
	/* Initialize char device file */
	
	// Allocate device number
	if ( (ret = alloc_chrdev_region(&tdev->device_num,0,1,"telemetry_rec")) )
		goto free_ring_buf;
		
	// cdev creation
	cdev_init(&tdev->cdev, &telemetry_drv_fops);
	tdev->cdev.owner = THIS_MODULE;
	
	if ( (ret = cdev_add(&tdev->cdev,tdev->device_num,1)) )
		goto unreg_chrdev;
		
	// class creation
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,0,0) 
	tdev->telem_class = class_create(THIS_MODULE,"telemetry_rec");
#else
	tdev->telem_class = class_create("telemetry_rec");
#endif
	if(IS_ERR(tdev->telem_class))
	{
		ret = PTR_ERR(tdev->telem_class);
		goto del_cdev;
	}
	
	// Device creation
	tdev->device = device_create(tdev->telem_class, NULL, 
					tdev->device_num, NULL, "telemetry_rec");
	if (IS_ERR(tdev->device))
	{
		ret = PTR_ERR(tdev->device);
		goto class_del;
	}
	
	printk(KERN_INFO "Telemetry device created successfully\n");
	
	return 0;
	
	/*cleanup path in case of init failure*/

class_del:
	class_destroy(tdev->telem_class);
del_cdev:
	cdev_del(&tdev->cdev);
unreg_chrdev:
	unregister_chrdev_region(tdev->device_num,1);
free_ring_buf:	
	ring_destroy(&tdev->buf);
free_dev:
	kfree(tdev);
	
	return ret;
}



static void __exit telemetry_dev_exit(void)
{
	if (!tdev) 
		return;
	device_destroy(tdev->telem_class, tdev->device_num);
	class_destroy(tdev->telem_class);
	cdev_del(&tdev->cdev);
	unregister_chrdev_region(tdev->device_num,1);
	
	if (tdev->producer_thread != NULL)	
		kthread_stop(tdev->producer_thread);
	ring_destroy(&tdev->buf);
	kfree(tdev);
	
	printk(KERN_INFO "Telemetry exit\n");
	
	return;
}





module_init(telemetry_dev_init);
module_exit(telemetry_dev_exit);
