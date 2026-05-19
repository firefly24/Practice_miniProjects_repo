#pragma once

#include <opencv2/opencv.hpp>
#include <iostream>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
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
		
		// Threshold sliders for canny edge detection
		int thres1=50, thres2 = 150, max_thres = 255;
		cv::namedWindow("Camera feed");
		cv::createTrackbar("Thresh 1", "Camera feed", &thres1, max_thres, 0 );
		cv::createTrackbar("Thresh 2", "Camera feed", &thres2, max_thres, 0 );
		
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
			
			/******* Add processing stage ***********************/
			thres1 = cv::getTrackbarPos("Thresh 1", "Camera feed");
			thres2 = cv::getTrackbarPos("Thresh 2", "Camera feed");
			cv::Mat processed_img;
			
			processImage(snapshot->get_image(), processed_img,thres1,thres2);
			
			/******* Record the frame in stats logs *************/
			display_time = std::chrono::steady_clock::now();
			stats_.record_frame(snapshot->get_timestamp(),display_time,
						snapshot->get_seq());
						
			last_seen_seq_ = snapshot->get_seq();
			
			/******** display the frame **************************/
			//cv::imshow("Camera feed", snapshot->get_image());
			cv::imshow("Camera feed", processed_img);
			
			//std::this_thread::sleep_for(
			//		std::chrono::milliseconds(250));
			
			/******* Wait before next refresh********************/
			int key = cv::waitKey(screen_refresh_ms);
			if (key == 'q')
			{
				std::cout << "Key q is pressed, quitting stream" << std::endl;
				signal_stop();
				break;
			}
		}
	}
	
	void processImage(const cv::Mat& src, cv::Mat &dest,int &thres1, int& thres2)
	{
		// convert to grayscale
		cv::cvtColor(src,dest, cv::COLOR_BGR2GRAY);
		
		// Blur to reduce noise
		cv::GaussianBlur(dest,dest, cv::Size(7,7),0 );
		
		// Canny edge detection 
		//cv::Canny(dest, dest, 50,150);
		cv::Canny(dest, dest, thres1,thres2);
		
		// finding Contours
		std::vector<std::vector<cv::Point>> contours;
		std::vector<cv::Vec4i> hierarchy;
		cv::Mat edges_for_contours = dest.clone();
		
		cv::findContours(edges_for_contours,
				 contours,
				 hierarchy,
				 cv::RETR_EXTERNAL,
				 cv::CHAIN_APPROX_SIMPLE);
				 
		// Draw contours on a color copt of the original
		dest = src.clone();
		cv::drawContours(dest,contours,-1,cv::Scalar(0,255,0),2);
		
		return;
	}
	
	void signal_stop()
	{
		running_.store(false, std::memory_order_release);
		stats_.stop_print_stats();
	}
};
