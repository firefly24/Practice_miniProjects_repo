#pragma once

#include <linux/wait.h>
#include <linux/err.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/types.h>
//#include <linux/vmalloc.h>


struct telemetry_record{
	
	uint64_t seq_no;
	uint64_t timestamp_ms;
	uint32_t value;
};

struct telemetry_ring_buf{

	struct telemetry_record *records;
	//uint32_t buf_size;
	uint32_t capacity;
	
	/* ring buffer indexing*/
	uint32_t read_idx;	// head of ring buffer 
	uint32_t write_idx;	// tail of ring buffer
	
	atomic_t available_records;
	
	// TODO: how do we track if current ring buffer is to be destroyed
};


int ring_init(struct telemetry_ring_buf *buf, uint32_t capacity);

bool ring_full(struct telemetry_ring_buf *buf);

bool ring_empty(struct telemetry_ring_buf *buf);

void ring_reset(struct telemetry_ring_buf *buf);

ssize_t records_available(struct telemetry_ring_buf *buf);

ssize_t ring_push(struct telemetry_ring_buf *buf,
		struct telemetry_record *record);

ssize_t ring_pop(struct telemetry_ring_buf *buf,
		struct telemetry_record *record);

void ring_destroy(struct telemetry_ring_buf *buf);















