#pragma once

#include <linux/types.h>
#include <linux/printk.h>
#include <linux/minmax.h>


struct telemetry_stats{
	uint64_t records_generated;
	uint64_t records_consumed;
	uint64_t records_dropped;
	
	uint32_t max_occupancy_seen;
	uint32_t ring_capacity;
	
	uint64_t producer_block_ctr;
	uint64_t consumer_block_ctr;
};

void telemetry_stats_init(struct telemetry_stats *stats,uint32_t);

void telemetry_stats_reset(struct telemetry_stats *stats);

void telemetry_stats_generated(struct telemetry_stats *stats);

void telemetry_stats_consumed(struct telemetry_stats *stats,uint32_t);

void telemetry_stats_producer_blocked(struct telemetry_stats *stats);

void telemetry_stats_consumer_blocked(struct telemetry_stats *stats);

/* Not used as of now, as we don't have any dropping strategy yet */
void telemetry_stats_dropped(struct telemetry_stats *stats);

void telemetry_stats_max_occupancy(struct telemetry_stats *stats, uint32_t occupancy);

void telemetry_stats_dump(struct telemetry_stats *stats);
