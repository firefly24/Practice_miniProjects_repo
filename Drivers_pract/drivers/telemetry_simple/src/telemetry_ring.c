#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/err.h>

#include "telemetry_ring.h"


/** INVARIANTS 
1. Current ring design is valid for SPSC model only, it will break for multiple readers/writers 
2. Producer pushes new record at tail, so only producer is allowed to modify buffer tail through push
3. Consumer can only pop from ring, so head should only be modified by consumer
*/

int ring_init(struct telemetry_ring_buf *buf, uint32_t capacity)
{	
	//buf->records = vmalloc(sizeof(struct telemetry_record) * (capacity+1));
	buf->records = kcalloc(capacity+1, sizeof(struct telemetry_record), GFP_KERNEL);
	
	// Allocate buffer
	if (!buf->records)
	{
		printk(KERN_ERR "Failed to allocate ring buffer\n");
		return -ENOMEM;
	}
	
	//buf->buf_size = 0;
	buf->capacity = capacity+1;
	ring_reset(buf);

	printk(KERN_INFO "Buffer allocated successfully\n");
	return 0;
}

void ring_reset(struct telemetry_ring_buf *buf)
{
	atomic_set(&buf->available_records,0);
	WRITE_ONCE(buf->read_idx ,0);
	WRITE_ONCE(buf->write_idx,0);
	
	return;
}

bool ring_full(struct telemetry_ring_buf *buf)
{
	return ((READ_ONCE(buf->write_idx)+1)%buf->capacity == READ_ONCE(buf->read_idx));
}

bool ring_empty(struct telemetry_ring_buf *buf)
{
	return (READ_ONCE(buf->write_idx) == READ_ONCE(buf->read_idx));
}



ssize_t ring_push(struct telemetry_ring_buf *buf,struct telemetry_record *record)
{
	uint32_t next_tail;

	// TODO: how do we trigger push from producer when space is available
	
	
	// fail push if no space on ring buffer
	if(ring_full(buf))
		return -ENOSPC;
	
	// TODO: how do we construct record in place in the allocated buffer instead of copying already constructed buffer
	
	// copy new record to ring		
	buf->records[buf->write_idx] = *record;
	
	// publish record and increment index
	next_tail = (buf->write_idx+1)%buf->capacity;
	WRITE_ONCE(buf->write_idx,next_tail);
	atomic_inc(&buf->available_records);
	
	// TODO: who will notify that new data is available now 
	
	return 0;
}


ssize_t records_available(struct telemetry_ring_buf *buf)
{
	return atomic_read(&buf->available_records);
	//return 0;
}

ssize_t ring_pop(struct telemetry_ring_buf *buf,struct telemetry_record *record)
{
	uint32_t next_head ;
	// TODO: how to we flush entire buffer instead of single record
	
	// fail read operation if ring is empty
	if( ring_empty(buf) )
		return -ENODATA;
		
	// extract needed record
	*record = buf->records[buf->read_idx];
	
	//publish new head
	next_head = (READ_ONCE(buf->read_idx)+1) % buf->capacity;
	WRITE_ONCE(buf->read_idx, next_head);
	atomic_dec(&buf->available_records);
	
	//TODO: who will inform that free space is available now
	
	return 0;
}

void ring_destroy(struct telemetry_ring_buf *buf)
{
	if(!buf->records)
		kfree(buf->records);
	
	buf->records = NULL;
}















