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
#include "telemetry_ring.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("firelfy24");
MODULE_DESCRIPTION("Simple telemetry module for practice");

#define PRODUCER_SLEEP_MS 1000

struct telemetry_dev{
	
	// device related
	dev_t device_number;
	struct cdev cdev;
	
	// buffer for data transfer
	struct telemetry_ring_buf buf;
	
	/* buffer activity notification for producers/consumers */
	wait_queue_head_t has_data_wq;
	wait_queue_head_t has_space_wq;
	
	// producer related
	struct task_struct *producer_thread;
	

	// Ownership and lifecycle tracking
	bool shutdown_reqested;
	bool device_open;
	
	// device info
	uint64_t seq_no;
};

struct telemetry_dev *tdev;

static int producer_thread_fn(void *data)
{
	struct telemetry_dev *tdev = data;
	struct telemetry_record *record;
	uint64_t seq_no =0;
	
	if(data == NULL)
		return -EINVAL;
		
	if (tdev->buf.records == NULL)
		return -EINVAL;
	
	record = kzalloc(sizeof(struct telemetry_record), GFP_KERNEL);
	
	if (!record)
		return -ENOMEM;
	
	while(!kthread_should_stop())
	{
		// generate record
		record->seq_no = seq_no++;
		record->timestamp_ms = ktime_to_ms(ktime_get());
		record->value = 42;
		
		// push to ring buffer
		//ring_push(&tdev->buf, record);
		if ( ring_push(&tdev->buf,record) == 0 )
		{
			printk(KERN_INFO "Successfully pushed: %llu\n",record->seq_no );
		}
		else
		{
			printk(KERN_INFO "Failed to push%llu\n",record->seq_no);
		}
		
		//sleep 
		msleep(PRODUCER_SLEEP_MS);
	}
	
	printk(KERN_INFO "Stopping producer thread\n");
	
	kfree(record);
	
	return 0;
}

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
		
	/* Create and initialize kthread */
	tdev->producer_thread = kthread_create(producer_thread_fn,(void *) tdev,"Producer_telemetry");
	
	if(IS_ERR(tdev->producer_thread))
	{
		ret = PTR_ERR(tdev->producer_thread);
		goto free_ring_buf;
	}
	
	/* run the producer thread */
	wake_up_process(tdev->producer_thread);
	
	// Wait for some time to let producer thread run
	msleep(10000);
	
	// stop producer thread
	kthread_stop(tdev->producer_thread);
	
	// set producer thread to NULL to avoid double kthread_stop
	tdev->producer_thread = NULL;
	
	return 0;
	
	
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
	if (tdev->producer_thread != NULL)	
		kthread_stop(tdev->producer_thread);
	ring_destroy(&tdev->buf);
	kfree(tdev);
	
	printk(KERN_INFO "Telemetry exit\n");
	
	return;
}





module_init(telemetry_dev_init);
module_exit(telemetry_dev_exit);
