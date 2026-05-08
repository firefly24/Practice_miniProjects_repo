#pragma once

#include <opencv2/opencv.hpp>
#include <iostream>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include "../frame_shared_state.h"


class Consumer{

private:
	FrameSharedState& shared_state_;
	std::atomic<bool>& running_;
	int screen_refresh_ms;

public:
	Consumer(FrameSharedState& shared, std::atomic<bool>& running, int refresh_time = 20)
	: shared_state_(shared), running_(running), screen_refresh_ms(refresh_time) {}
	
	void run()
	{
		while(running_.load(std::memory_order_acquire))
		{
			std::shared_ptr<Frame> snapshot = 
						shared_state_.get_latest_frame();
						
			if(!snapshot)
			{
				std::this_thread::sleep_for(
					std::chrono::milliseconds(5));
				
				continue;
			}
			
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
	}
};
