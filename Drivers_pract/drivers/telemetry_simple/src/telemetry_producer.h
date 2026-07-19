#pragma once

#include <linux/types.h>
#include <linux/kthread.h>

struct telemetry_dev;

struct telemetry_producer {

	struct task_struct *thread;
	
	uint32_t id;
	
	struct telemetry_dev *parent;
	
	uint64_t seq_no;
	
	bool is_active;
};

void telemetry_producer_init(struct telemetry_producer *producer,struct telemetry_dev *tdev);

int telemetry_producer_start(struct telemetry_producer *producer);

void telemetry_producer_stop(struct telemetry_producer *producer);

int telemetry_active_producers(void);

