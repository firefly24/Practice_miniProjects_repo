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

void telemetry_stats_update_max_occupancy(struct telemetry_stats *stats, uint32_t occupancy)
{
	if(!stats)
		return;
		
	stats->max_occupancy_seen = max_t(uint32_t, stats->max_occupancy_seen, occupancy);
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
	printk(KERN_INFO "===========================\n");
}








