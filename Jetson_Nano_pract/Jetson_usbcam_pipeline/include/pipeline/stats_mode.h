#pragma once

#include <string>
#include <chrono>

using Timepoint = std::chrono::steady_clock::time_point;

#ifdef ENABLE_STATS

#include "pipeline_stats.h"
using StatsType = PipelineStats;

#else

// Define a no-op class to be used in perf mode
class StatsType {

public:

	StatsType(std::string = "") {}

	void record_frame(const Timepoint&, const Timepoint ,const int&) {}
	void record_null_frame() {}
	void record_read_failure() {}
	
	void start_print_stats(int period = 1000) {}
	void stop_print_stats() {}
	void print_lifetime_stats() {}
};

#endif
