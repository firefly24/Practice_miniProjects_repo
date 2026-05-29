#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <atomic>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include "../frame_shared_state.h"
#include "stats_mode.h"
#include "../../models/labelsimagenet1k.h"

using Timepoint = std::chrono::steady_clock::time_point;

class PrevFrameState {

public:
	cv::Mat gray_img;
	std::vector<cv::Point2f> corners;
	std::vector<cv::Point2f> good_corners;
	bool need_init = true;
	
};

class Consumer{

private:
	FrameSharedState& shared_state_;
	std::atomic<bool>& running_;
	int last_seen_seq_;
	int screen_refresh_ms;
	PrevFrameState prev_frame_;
	
	// For inference pipeline
	cv::dnn::Net classifier_;

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

		cv::namedWindow("Camera feed");
		
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
			cv::Mat processed_img;
			
			//processImageCorners(snapshot->get_image(), processed_img);
			processImage(snapshot->get_image(), processed_img);
			
			/******* Record the frame in stats logs *************/
			display_time = std::chrono::steady_clock::now();
			stats_.record_frame(snapshot->get_timestamp(),display_time,
						snapshot->get_seq());
						
			last_seen_seq_ = snapshot->get_seq();
			
			/******** display the frame **************************/
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
	
	void runInference()
	{
		Timepoint display_time;

		cv::namedWindow("Camera feed");
		
		classifier_ = cv::dnn::readNetFromONNX("/home/darshana/practice/Practice_miniProjects_repo/Jetson_Nano_pract/Jetson_usbcam_pipeline/models/image_classification_mobilenetv2_2022apr.onnx");
		
		// how to handle failure exception here ? 
		
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
			//cv::Mat processed_img;
			
			processImageClass(snapshot->get_image());
			
			/******* Record the frame in stats logs *************/
			display_time = std::chrono::steady_clock::now();
			stats_.record_frame(snapshot->get_timestamp(),display_time,
						snapshot->get_seq());
						
			last_seen_seq_ = snapshot->get_seq();
			
			/******** display the frame **************************/
			cv::imshow("Camera feed", snapshot->get_image());
			
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
	
	void processImageClass(const cv::Mat& src)
	{
		/*	Preprocess input to resize*/
		cv::Mat blob = cv::dnn::blobFromImage(src,
						1.0/255.0, 
						cv::Size(224,224),
						cv::Scalar(),true,false);
		
		/*	Run Inference	*/
		classifier_.setInput(blob);
		cv::Mat prob = classifier_.forward();
		
		
		/* find class with highest probability*/
		double maxProb;
		cv::Point classIdPoint;
		cv::minMaxLoc(prob.reshape(1,1),0,&maxProb,0,&classIdPoint);
		
		auto labels = getLabelsImagenet1k();
		
		/* std::cout << "Predicted Class ID: " << labels[classIdPoint.x] 
		 	<< " with confidence " << maxProb << std::endl;
    		*/
    		return ;
	}
	
	//Contour detection
	void processImage(const cv::Mat& src, cv::Mat &dest)
	{
		cv::Mat gray;
		// convert to grayscale
		cv::cvtColor(src,gray, cv::COLOR_BGR2GRAY);
		
		// Blur to reduce noise
		cv::GaussianBlur(gray,dest, cv::Size(7,7),0 );
		
		// Canny edge detection 
		cv::Canny(dest, dest, 50,150);
		
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
		cv::cvtColor(dest,dest, cv::COLOR_GRAY2BGR);
		cv::drawContours(dest,contours,-1,cv::Scalar(0,255,0),2);
		
		return;
	}
	
	/** Function for corner detection to increase the cpu compute load, not given too much thought into the accuracy and tuning yet, maybe will take it up later 
	*/
	
	void processImageCorners(const cv::Mat& src, cv::Mat &dest)
	{
		std::vector<cv::Point2f> corners;
		cv::Mat grayimg;
		cv::cvtColor(src,grayimg,cv::COLOR_BGR2GRAY);
		
		if ( prev_frame_.gray_img.empty() || prev_frame_.corners.size()<20)
		{
			// detect corners
			cv::goodFeaturesToTrack( grayimg, prev_frame_.corners,
						80,	// max number of corners to return
						0.01,	// quality threshold, more corners with smaller value
						 25	// min distance between corners
						);
			
			dest = src.clone();
			for(const auto& pt: prev_frame_.corners)
			{
				cv::circle(dest,pt,4,cv::Scalar(0,255,0),2);
			}
			
			prev_frame_.gray_img = grayimg.clone();
			prev_frame_.need_init = false;
			
			return;
		}
		
		std::vector<uchar> status;
		std::vector<float> err;
		
		cv::calcOpticalFlowPyrLK( prev_frame_.gray_img,
					  grayimg,
					  prev_frame_.corners,
					  corners,
					  status,
					  err
					);
		
		if (corners.size() < 20)
		{
					// detect corners
			cv::goodFeaturesToTrack( grayimg, prev_frame_.corners,
						100,	// max number of corners to return
						0.01,	// quality threshold, more corners with smaller value
						 20	// min distance between corners
						);
			
			dest = src.clone();
			for(const auto& pt: prev_frame_.corners)
			{
				cv::circle(dest,pt,4,cv::Scalar(0,255,0),2);
			}
			
			prev_frame_.gray_img = grayimg.clone();
			prev_frame_.need_init = false;
			
			return;
		
		}
		
		dest = src.clone();
		prev_frame_.good_corners = {};
		//for(const auto& pt: corners)
		for (size_t i =0; i<corners.size();i++)
		{
			if (!status[i])
				continue;
				
			cv::circle(dest,corners[i],4,cv::Scalar(0,255,0),2);
			cv::line(dest,prev_frame_.corners[i], corners[i],cv::Scalar(0,0,255),2);
			prev_frame_.good_corners.push_back(corners[i]);
		}
		
		prev_frame_.gray_img = grayimg.clone();
		prev_frame_.corners = prev_frame_.good_corners;
		
		return;
	}
	
	void signal_stop()
	{
		running_.store(false, std::memory_order_release);
		stats_.stop_print_stats();
	}
};































