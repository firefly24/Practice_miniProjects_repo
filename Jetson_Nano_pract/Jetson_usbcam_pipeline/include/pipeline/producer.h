#pragma once

#include <opencv2/opencv.hpp>
#include <atomic>
#include <memory>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstdint>
#include "../frame_shared_state.h"
#include "../capture_source.h"

using Timepoint = std::chrono::steady_clock::time_point;

#define RETRY_COUNT 5

class Producer {

private:
	CaptureSource capture_;
	FrameSharedState& shared_state_;
	std::atomic<bool>& running_;
	int seq_;
	int device_id_;

public:	
	Producer(FrameSharedState& shared, 
		std::atomic<bool>& running,
		int device_id =0)
	: shared_state_(shared), running_(running), seq_{0}, device_id_{device_id}
	{
		/* TODO: document why we don't use shared pointer for FrameSharedState and running, but use a reference to these objects from main(). Whereas we use shared_ptr for the actual Frame object inside of this FrameSharedState
		*/		
	}
	
	void run()
	{
		cv::Mat img;
		int retry_count = RETRY_COUNT;
		
		//Failed to open capture device
		if(!capture_.open(device_id_))
		{
			std::cout << "Producer: Failed to open capture source" 
					<< std::endl;
			signal_stop();
			return;
		}
		
		// now read image in loop
		while(running_.load(std::memory_order_acquire))
		{
			// capture image from source
			if ( !capture_.read(img) )
			{
				if (!(retry_count--)){
					signal_stop();
					break;
				}
				
				// sleep for 5ms then loop again
				std::this_thread::sleep_for(
					std::chrono::milliseconds(5));
					
				continue;
			}
			retry_count = RETRY_COUNT;
			
			Timepoint now = std::chrono::steady_clock::now();
			
			// Capture successful, bind this to shared state
			std::shared_ptr<Frame> current_frame =
				std::make_shared<Frame>(img,now,seq_);
			seq_++;
			shared_state_.publish(current_frame);
		}
		std::cout << "Total frames rendered: " << seq_ << std::endl;
		capture_.close();		
	}
	
	void signal_stop()
	{
		running_.store(false, std::memory_order_release);
		std::cout << "Total frames rendered: " << seq_ << std::endl;
	}
	
};
