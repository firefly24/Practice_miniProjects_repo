#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/err.h>

#include <linux/slab.h>
#include <linux/fs.h>

#include "telemetry_dev.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("firelfy24");
MODULE_DESCRIPTION("Simple telemetry module for practice");

static int bp_policy = TELEMETRY_BP_BLOCK;
module_param(bp_policy, int,0644);
MODULE_PARM_DESC(bp_policy, "Set backpressure policy for ring overflow");


/** Invariants: 
- Init producer always after ring buffer allocation, and stop producer before ring free	
*/ 

struct telemetry_dev *tdev;

/*****************************************************************************/


/*Since record push will be done by producer module, it cannot be static anymore. So any other program can attempt to call this directly without any producer, how to handle the security here? */
int telemetry_push_record(struct telemetry_dev *tdev, struct telemetry_record *record)
{
	int ret=0;
	
	if (!tdev->buf.records)
	{
		pr_err("Illegal push before buffer initialzed!\n");
		return -EPERM;
	}

	
	while( (ret = ring_push(&tdev->buf,record)) )
	{	
		//record the number of times producer is blocked wating for queue space
		telemetry_stats_producer_blocked(&tdev->stats);
		
		if (ret != -ENOSPC) 
		{
			pr_err("Failed to push%llu, reason: %u \n",record->seq_no,ret);
			return ret;
		}
		
		// Ring is full if push return ENOSPC, apply backpressure
		
		switch(tdev->backpressure_policy)
		{
			case TELEMETRY_BP_BLOCK:
			
				// implement block producer policy here 
				ret = wait_event_interruptible(tdev->has_space_wq,
				!ring_full(&tdev->buf) || READ_ONCE(tdev->shutdown_session));
				
				// interuupted by a signal
				if(ret)
					return ret;
		
				if(READ_ONCE(tdev->shutdown_session))
					return 0;
				break;
			
			case TELEMETRY_BP_DROP_NEW:
				// implement drop new records policy
				telemetry_stats_dropped(&tdev->stats);
				pr_info("DROP_NEW: incoming record discarded\n");
				return 0;
			
			case TELEMETRY_BP_DROP_OLD:
				// implement drop old records policy
				// TODO - to be implementated later after adding ring synchronization
				return -ENOTSUPP;
				
			default:
				pr_err("Invalid backpressure policy selected\n");
				return -EINVAL;
		}	
	}

	// push to ring buffer is successful
	
	// TODO: type mismatch between occupancy parameter 
	telemetry_stats_generated(&tdev->stats);
	telemetry_stats_max_occupancy(&tdev->stats,records_available(&tdev->buf));
	wake_up_interruptible(&tdev->has_data_wq);
	
	pr_info("Successfully pushed: %llu\n",record->seq_no );	
	
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
		pr_err( "Device is already opened by another process\n");
		return -EBUSY;
	}
	
	dev_file->private_data = tdev;
	tdev->has_owner = true;
	
	spin_unlock(&tdev->ownership_lock);
	
	WRITE_ONCE(tdev->shutdown_session,false);
	
	telemetry_stats_reset(&tdev->stats);
	
	pr_info("Open: Acquired telemetry device ownership\n");
	
	/* Run the producer thread after taking ownership*/
	if ( (ret = telemetry_producer_start(&tdev->producer)) )
	{
		// release ownership if producer thread fails
		spin_lock(&tdev->ownership_lock);
		tdev->has_owner = false;
		spin_unlock(&tdev->ownership_lock);
		
		pr_err("Producer launch failed! Abort session!\n");
	}
	
	return ret;
}

static int telemetry_release(struct inode *filenode, struct file *dev_file)
{
	struct telemetry_dev *tdev = dev_file->private_data;
	
	WRITE_ONCE(tdev->shutdown_session,true);
	wake_up_all(&tdev->has_data_wq);
	wake_up_all(&tdev->has_space_wq);
	
	/* stop producer thread and complete work before releasing ownership*/
	telemetry_producer_stop(&tdev->producer);
	
	ring_reset(&tdev->buf);
	
	// let's dump session stats before releasing session
	telemetry_stats_dump(&tdev->stats);
	
	/* release ownership of device */
	spin_lock(&tdev->ownership_lock);
	tdev->has_owner = false;
	spin_unlock(&tdev->ownership_lock);
	
	pr_info("Released telemetry device ownership\n");
	
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
	size_t records_requested = data_size/ sizeof(struct telemetry_record);
	
	// This read version will only work for single producer single consumer model
	
	if(records_requested <=0)
		return -EINVAL;
		
	//record the number of times consumer is blocked due to empty queue
	// this may have some races
	if (ring_empty(&tdev->buf))
		telemetry_stats_consumer_blocked(&tdev->stats);
		
	// Block until data is available in the ring
	ret = wait_event_interruptible(tdev->has_data_wq, 
						!ring_empty(&tdev->buf) 
						|| READ_ONCE(tdev->shutdown_session));
	
	// Read interrupted by a signal
	if(ret)
		return ret; 
		
	if(READ_ONCE(tdev->shutdown_session))
		return 0;
	
	records_to_return = min_t(size_t,records_available(&tdev->buf), records_requested);
	bytes_to_copy = records_to_return*sizeof(struct telemetry_record);
	
	if (records_to_return <= 0)
		return -ENODATA;
	
	records = kcalloc(records_to_return,sizeof(struct telemetry_record), GFP_KERNEL);
	if (!records)
		return -ENOMEM;

	// TODO: How to avoid multiple copies of record data here
		
	while(idx< records_to_return)
	{
		ring_pop(&tdev->buf,&records[idx++]); 
	}
	// TODO: parameter type mismatch
	telemetry_stats_consumed(&tdev->stats,records_to_return);
	wake_up_interruptible(&tdev->has_space_wq);
	
	// Since telemetry buffer is ring buffer, we cannot guarantee contiguous data in memory, so using a temporary buffer to copy availble data first
	ret = copy_to_user(user_buf,records,bytes_to_copy);
	if(ret)
	{
		// records popped successfully from queue, but failed to deliver to consumer
		// those records will be lost forever, (no peek semantics)
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

	if(bp_policy < 0 || bp_policy >= TELEMETRY_BP_MAX)
	{
		pr_err("Please select valid backpressure policy\n");
		return -EINVAL;
	}
	
	tdev = kzalloc(sizeof(struct telemetry_dev),GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;
	
	pr_info("Telemetry_init\n");
		
	/* Create and initialize ring buffer for telemetry records */
	if ((ret = ring_init(&tdev->buf, capacity)) )
		goto free_dev;
	
	/* zero initialize values needed in open() call */
	
	telemetry_producer_init(&tdev->producer,tdev);
	tdev->has_owner = false;
	spin_lock_init(&tdev->ownership_lock);
	init_waitqueue_head(&tdev->has_data_wq);
	init_waitqueue_head(&tdev->has_space_wq);
	WRITE_ONCE(tdev->shutdown_session,false);
	telemetry_stats_init(&tdev->stats,capacity);
	tdev->backpressure_policy = bp_policy;
		
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
	if(IS_ERR(tdev->telem_class)) {
		ret = PTR_ERR(tdev->telem_class);
		goto del_cdev;
	}
	
	// Device creation
	tdev->device = device_create(tdev->telem_class,
					NULL, 
					tdev->device_num,
					NULL, 
					"telemetry_rec");
	if (IS_ERR(tdev->device)) {
		ret = PTR_ERR(tdev->device);
		goto class_del;
	}
	
	pr_info("Telemetry device created successfully\n");
	
	if (telemetry_dbgfs_init(tdev))
		pr_warn("Failed to init debugfs attributes\n");
	
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
	telemetry_dbgfs_cleanup(tdev);
	device_destroy(tdev->telem_class, tdev->device_num);
	class_destroy(tdev->telem_class);
	cdev_del(&tdev->cdev);
	unregister_chrdev_region(tdev->device_num,1);
	
	telemetry_producer_stop(&tdev->producer);
	ring_destroy(&tdev->buf);
	kfree(tdev);
	
	pr_info("Telemetry exit\n");
	
	return;
}





module_init(telemetry_dev_init);
module_exit(telemetry_dev_exit);
