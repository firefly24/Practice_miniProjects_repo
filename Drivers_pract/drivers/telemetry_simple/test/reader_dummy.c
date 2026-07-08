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
		{
			perror("Read error!");
			break;
		}
		
		num_records = n/ record_size;
		
		printf("Num records read: %d\n", num_records);
		
		for(i=0;i<num_records;i++)
		{
			printf("seq: %lu, value: %u\n",rec[i].seq_no, rec[i].value);
		}
		sleep(1);
	}

	return 0;
}

