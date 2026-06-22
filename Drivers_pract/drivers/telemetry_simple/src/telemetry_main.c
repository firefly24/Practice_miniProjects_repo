#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include "telemetry_ring.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("firelfy24");
MODULE_DESCRIPTION("Simple telemetry module for practice");

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

void test_push_pop(void)
{
	int capacity,i=0;
	struct telemetry_record *record;
	
	record = kzalloc(sizeof(struct telemetry_record), GFP_KERNEL);
	
	if(!record)
		return ;//-ENOMEM;
		
	capacity= tdev->buf.capacity;
	
	for (i=0;i<=capacity;i++)
	{
		record->value = i *10;
		record->seq_no = i;
		record->timestamp_ms = 0;
		if(ring_push(&tdev->buf,record))
		{
			printk(KERN_INFO "Fail insert %d\n",i);
		}
	}
	
	for(i=0;i<=capacity;i++)
	{
		if ( ring_pop(&tdev->buf,record) == 0 )
		{
			printk(KERN_INFO "Successfully popped: %d\n",record->value );
		}
		else
		{
			printk(KERN_INFO "Failed to pop%d\n",i);
		}
	
	}
	
	kfree(record);
	
	return;
}

static int __init telemetry_dev_init(void)
{
	int ret=0;
	uint32_t capacity = 4;
	
	tdev = kzalloc(sizeof(struct telemetry_dev),GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;
	
	printk(KERN_INFO "Telemetry_init\n");
		
	if ((ret = ring_init(&tdev->buf, capacity)) )
		return  ret;
	
	test_push_pop();
	
	return 0;
}

static void __exit telemetry_dev_exit(void)
{

	ring_destroy(&tdev->buf);
	kfree(tdev);
	
	printk(KERN_INFO "Telemetry exit\n");
	
	return;
}





module_init(telemetry_dev_init);
module_exit(telemetry_dev_exit);
