#pragma once

#include <opencv2/opencv.hpp>
#include <iostream>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include "../frame_shared_state.h"
#include "stats_mode.h"

using Timepoint = std::chrono::steady_clock::time_point;

class Consumer{

private:
	FrameSharedState& shared_state_;
	std::atomic<bool>& running_;
	int last_seen_seq_;
	int screen_refresh_ms;

public:
	StatsType& stats_;
	
	Consumer(FrameSharedState& shared, 
		std::atomic<bool>& running, 
		StatsType& stats,
		int refresh_time = 20)
	: shared_state_(shared), running_(running),
	screen_refresh_ms(refresh_time), last_seen_seq_(-1),
	stats_(stats) {}
	
	void run()
	{
		Timepoint display_time;
		while(running_.load(std::memory_order_acquire))
		{
			std::shared_ptr<Frame> snapshot = 
						shared_state_.get_latest_frame();
						
			if(!snapshot)
			{
				stats_.record_null_frame();
				std::this_thread::sleep_for(
					std::chrono::milliseconds(5));
				
				continue;
			}
			
			if (last_seen_seq_ == snapshot->get_seq())
			{
				std::this_thread::sleep_for(
					std::chrono::milliseconds(1));
				continue;
			}
			
			// record the frame in logs
			display_time = std::chrono::steady_clock::now();
			stats_.record_frame(snapshot->get_timestamp(),display_time,
						snapshot->get_seq());
						
			last_seen_seq_ = snapshot->get_seq();
						
			// display the frame
			cv::imshow("Camera feed", snapshot->get_image());
			
			int key = cv::waitKey(screen_refresh_ms);
			
			if (key == 'q')
			{
				std::cout << "Key q is pressed, quitting stream" << std::endl;
				signal_stop();
				break;
			}
		}
	}
	
	void signal_stop()
	{
		running_.store(false, std::memory_order_release);
		stats_.stop_print_stats();
	}
};
