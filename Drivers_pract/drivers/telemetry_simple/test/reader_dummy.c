#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

struct telemetry_record{
	
	uint64_t seq_no;
	uint64_t timestamp_ms;
	uint32_t value;
};


int main(void)
{
	int fd;
	int num_records=0;
	int i=0;
	int record_size = sizeof(struct telemetry_record);
	
	struct telemetry_record rec[8];
	
	fd = open("/dev/telemetry_rec", O_RDONLY);
	if (fd < 0)
	{
		printf("Telemetry device open failed\n");
		return -1;
	}
	
	while(1)
	{
		ssize_t n = read(fd,rec,sizeof(rec));
		
		if (n<0)
			continue;
		
		num_records = n/ record_size;
		
		printf("Num records read: %d\n", num_records);
		
		for(i=0;i<num_records;i++)
		{
			printf("seq: %lu, value: %u\n",rec[i].seq_no, rec[i].value);
		}
		sleep(2);
		
		
	}

	return 0;
}

/*

#include "../telemetry_ring.h"

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
*/
