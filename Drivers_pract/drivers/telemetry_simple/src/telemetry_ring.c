#include "telemetry_ring.h"


int ring_init(struct telemetry_ring_buf *buf, uint32_t capacity)
{	
	//buf->records = vmalloc(sizeof(struct telemetry_record) * (capacity+1));
	buf->records = kcalloc(capacity+1, sizeof(struct telemetry_record), GFP_KERNEL);
	
	// Allocate buffer
	if (!buf->records)
	{
		printk(KERN_ERR "Vmalloc failed to allocate ring buffer\n");
		return -ENOMEM;
	}
	
	//buf->buf_size = 0;
	buf->capacity = capacity+1;
	
	// initialize indexes
	WRITE_ONCE(buf->read_idx,0);
	WRITE_ONCE(buf->write_idx,0);

	printk(KERN_INFO "Buffer allocated successfully\n");
	return 0;
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
	
	// TODO: who will notify that new data is available now 
	
	return 0;
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
	
	//TODO: who will inform that free space is available now
	
	return 0;
}

void ring_destroy(struct telemetry_ring_buf *buf)
{

	// TODO: how to wait for all dependent readers/writers to finish
	if(buf->records != NULL)
		kfree(buf->records);
	
	buf->records = NULL;
}















