

#include <linux/err.h>
#include <linux/ktime.h>
#include <linux/sched.h>
#include <linux/delay.h>

#include "telemetry_producer.h"
#include "telemetry_dev.h"


/* TODO: reconsider moving this responsibiltiy to telemetry_main*/
static uint32_t producers=0;

void telemetry_producer_init(struct telemetry_producer *producer,struct telemetry_dev *tdev)
{
	if(!producer || !tdev)
		return;
	
	producer->thread = NULL;
	producer->seq_no = 0;
	producer->parent = tdev;
	producer->id = producers++;
}

static void producer_generate_record ( struct telemetry_producer *producer, 
					struct telemetry_record *record)
{
	// generate record
	record->seq_no = producer->seq_no++;
	record->timestamp_ms = ktime_to_ms(ktime_get());
	record->value = 42;

}

static int producer_thread_fn(void *data)
{
	struct telemetry_producer *producer = data;
	struct telemetry_record record;
	int ret=0;
	
	if(!producer)
		return -EINVAL;
	
	pr_info("Starting Producer thread\n");
	
	while(!kthread_should_stop())
	{
		producer_generate_record(producer,&record);
		
		if ( (ret = telemetry_push_record(producer->parent,&record)) )
			break;
		
		//sleep 
		msleep(PRODUCER_SLEEP_MS);
	}
	
	pr_info("Stopped producer thread\n");
	
	return ret;
}

int telemetry_producer_start(struct telemetry_producer *producer)
{
	int ret= 0;
	
	if (!producer)
		return -EINVAL;
	
	/* Create and initialize kthread */
	producer->thread = kthread_create(producer_thread_fn,
						(void *) producer,
						"telemetry_prod_%u",producer->id);
	
	if(IS_ERR(producer->thread))
	{
		ret = PTR_ERR(producer->thread);
		producer->thread = NULL;
		return ret ;
	}
	
	/* run the producer thread */
	wake_up_process(producer->thread);
	
	return 0;
}

void telemetry_producer_stop(struct telemetry_producer *producer)
{
	if(producer->thread)
	{
		kthread_stop(producer->thread);
		producer->thread = NULL;
	}
}











