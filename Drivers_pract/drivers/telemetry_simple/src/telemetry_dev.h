#pragma once

#include <linux/types.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include "telemetry_ring.h"
#include "telemetry_producer.h"
#include "telemetry_stats.h"
#include "telemetry_debugfs.h"

#define PRODUCER_SLEEP_MS 100
#define RING_BUF_CAPACITY 5
#define DEFAULT_PRODUCERS_COUNT 16
#define MAX_PRODUCERS_COUNT

enum telemetry_backpressure_policy {
	TELEMETRY_BP_BLOCK,
	TELEMETRY_BP_DROP_NEW,
	TELEMETRY_BP_DROP_OLD,
	TELEMETRY_BP_MAX,
};

struct telemetry_dev{
	
	
	/*TODO: group the member together w.r.t:
					 module lifetime, 
					 per-session states, 
					 runtime data, 
					 observabiltiy etc
	*/
	
	/* Character device */
	dev_t device_num;
	struct cdev cdev;
	struct class *telem_class;
	struct device *device;
	
	/* Telemetry ring buffer*/
	struct telemetry_ring_buf buf;
	
	/* Ring access synchronization for producers/consumers */
	wait_queue_head_t has_data_wq;
	wait_queue_head_t has_space_wq;
	
	/* Producer */
	struct telemetry_producer *producers;
	uint32_t producer_count;
	
	enum telemetry_backpressure_policy backpressure_policy;
	
	/* Session state - ownership and lifecycle */
	bool shutdown_session;
	bool has_owner;
	spinlock_t ownership_lock;
	
	/* Driver statistics */
	struct telemetry_stats stats;
	
	/* DebugFS */
	struct telemetry_dbgfs dbgfs;
};


int telemetry_push_record(struct telemetry_dev *tdev, struct telemetry_record *record);



