#include <linux/printk.h>
#include <linux/minmax.h>
#include "telemetry_stats.h"


void telemetry_stats_init(struct telemetry_stats *stats,uint32_t capacity)
{
	if(!stats)
		return;
		
	telemetry_stats_reset(stats);
	
	stats->ring_capacity = capacity;

}

void telemetry_stats_reset(struct telemetry_stats *stats)
{
	if(!stats)
		return;
		
	stats->records_generated = 0;
	stats->records_consumed = 0;
	stats->records_dropped = 0;
	stats->max_occupancy_seen = 0;
	stats->producer_block_ctr = 0;
	stats->consumer_block_ctr = 0;
	//stats->ring_capacity = 0;
}

void telemetry_stats_generated(struct telemetry_stats *stats)
{
	if(!stats)
		return;
		
	stats->records_generated++;
}

void telemetry_stats_consumed(struct telemetry_stats *stats,uint32_t count)
{
	if(!stats)
		return;
		
	stats->records_consumed += count;
}

/* Not used as of now, as we don't have any dropping strategy yet */
void telemetry_stats_dropped(struct telemetry_stats *stats)
{

}

void telemetry_stats_max_occupancy(struct telemetry_stats *stats, uint32_t occupancy)
{
	if(!stats)
		return;
		
	stats->max_occupancy_seen = max_t(uint32_t, stats->max_occupancy_seen, occupancy);
}

void telemetry_stats_producer_blocked(struct telemetry_stats *stats)
{
	stats->producer_block_ctr++;
}

void telemetry_stats_consumer_blocked(struct telemetry_stats *stats)
{
	stats->consumer_block_ctr++;
}

void telemetry_stats_dump(struct telemetry_stats *stats)
{
	if(!stats)
		return;
	
	printk(KERN_INFO "===========================\n");
	printk(KERN_INFO "Telemetry Driver Statistics\n");
	printk(KERN_INFO "___________________________\n");
	printk(KERN_INFO "Records generated:\t %llu\n",stats->records_generated);
	printk(KERN_INFO "Records consumed:\t %llu\n",stats->records_consumed);
	printk(KERN_INFO "Records dropped:\t %llu\n",stats->records_dropped);
	printk(KERN_INFO "Max occupancy seen so far:\t (%u / %u)\n",
				stats->max_occupancy_seen, stats->ring_capacity);
	printk(KERN_INFO "Producer blocked count:\t %llu\n",stats->producer_block_ctr);
	printk(KERN_INFO "Consumer blocked count:\t %llu\n",stats->consumer_block_ctr);
	printk(KERN_INFO "===========================\n");
}

void telemetry_stats_show(struct telemetry_stats *stats,struct seq_file *s)
{
	seq_printf(s,"Hello Telemetry!\n");
	
	seq_printf(s,"This is my first debugfs file.\n");

	if(!stats)
		return;
	
	seq_printf(s, "===========================\n");
	seq_printf(s, "Telemetry Driver Statistics\n");
	seq_printf(s, "___________________________\n");
	seq_printf(s, "Records generated:\t %llu\n",stats->records_generated);
	seq_printf(s, "Records consumed:\t %llu\n",stats->records_consumed);
	seq_printf(s, "Records dropped:\t %llu\n",stats->records_dropped);
	seq_printf(s, "Max occupancy seen so far:\t (%u / %u)\n",
				stats->max_occupancy_seen, stats->ring_capacity);
	seq_printf(s, "Producer blocked count:\t %llu\n",stats->producer_block_ctr);
	seq_printf(s, "Consumer blocked count:\t %llu\n",stats->consumer_block_ctr);
	seq_printf(s, "===========================\n");
}





