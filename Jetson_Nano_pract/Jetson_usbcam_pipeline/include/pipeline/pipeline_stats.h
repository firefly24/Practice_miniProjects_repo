#pragma once

#include <iostream>
#include <string>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include "../frame.h"

using Timepoint = std::chrono::steady_clock::time_point;

class FrameStats{

public:
	unsigned int frame_count_;
	int dropped_frames_;
	unsigned int null_frames_;
	unsigned int read_failures_;
	int last_seq_;
	double fps_;
	double avg_latency_ms_;
	double max_latency_ms_;
	Timepoint start_time_;
	
		
	FrameStats(): frame_count_(0),dropped_frames_(0),
			null_frames_(0), read_failures_(0),last_seq_(-1),
			fps_(0.0), avg_latency_ms_(0.0),max_latency_ms_(0.0),
			start_time_(std::chrono::steady_clock::now()) {}
					 
	void reset_window(int last_seen_seq=-1)
	{
		frame_count_	=0;
		dropped_frames_	=0;
		null_frames_	=0;	
		read_failures_	=0;
		last_seq_	=last_seen_seq;
		fps_		=0.0;
		avg_latency_ms_	=0.0;
		max_latency_ms_	=0.0;
		start_time_	= std::chrono::steady_clock::now();
	}
	
	double elapsed_time_ms() const
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>
			(std::chrono::steady_clock::now() - start_time_).count();
	}
	
	void print_stats()
	{
		if (!frame_count_)
		{
			std::cout << "frames:" << frame_count_ 
			<< " dropped frames:" << dropped_frames_
			<< " null_frames:" << null_frames_
			<< std::endl;
			
			return;
		}
		
		fps_ = frame_count_ / (elapsed_time_ms()/1000.0);
		
		std::cout << "frames:" << frame_count_
			<< " fps:" << fps_ 
			<< " avg latency:" << (avg_latency_ms_/frame_count_)
			<< "ms max latency:" << max_latency_ms_
			<< "ms dropped frames:" << dropped_frames_
			<< " null_frames:" << null_frames_
			<< std::endl;
	}
};

class PipelineStats{

private:

	FrameStats windowed_stats_;
	FrameStats lifetime_stats_;
	std::fstream stats_file_;
	std::string filename_;
	
	
	std::atomic<bool> is_running{false};
	std::thread logger_thread;
	std::mutex mtx_;
	
public:

	PipelineStats(std::string filename):filename_(filename)
	{
		is_running.store(true, std::memory_order_release);
	}
	
	// Consumer must record every frame in the stats
	void record_frame(const Timepoint &capture_time,
				const Timepoint display_time,const int& seq)
	{
		mtx_.lock();
		// log lifetime stats now
		// give console output for now, log in csv later
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>
		(display_time - capture_time);
		
		// update max capture->display latency
		lifetime_stats_.max_latency_ms_ = std::max(
						lifetime_stats_.max_latency_ms_,
						(double)duration.count());
		
		//add dropped frame count from last displayed frame seq
		lifetime_stats_.dropped_frames_ += seq- lifetime_stats_.last_seq_ -1 ;
		
		// update the latest seen frame seq
		lifetime_stats_.last_seq_ = seq;
		
		// add latency to be used to calculate avg later
		lifetime_stats_.avg_latency_ms_ += duration.count();
		
		// update total frames recorded
		lifetime_stats_.frame_count_++;
		
		// log windowed stats, but how to calculate windowed stats ? 
		windowed_stats_.max_latency_ms_ = std::max(
						windowed_stats_.max_latency_ms_,
						(double)duration.count());
						
		windowed_stats_.dropped_frames_ =
					 (seq- windowed_stats_.last_seq_ -1) ;
		
		/*
		std::cout << seq << " " << windowed_stats_.last_seq_ 
			<< " " << duration.count()<<std::endl; 
		*/
		windowed_stats_.last_seq_ = seq;
		windowed_stats_.avg_latency_ms_ += duration.count();
		windowed_stats_.frame_count_++;
		
		mtx_.unlock();
	}
	
	void record_null_frame()
	{
		mtx_.lock();
		lifetime_stats_.null_frames_++;
		windowed_stats_.null_frames_++;
		mtx_.unlock();
	}
	
	void record_read_failure()
	{
		mtx_.lock();
		lifetime_stats_.read_failures_++;
		windowed_stats_.read_failures_++;
		mtx_.unlock();
	}
	
	void print_lifetime_stats()
	{
		FrameStats snapshot(lifetime_stats_);
		snapshot.print_stats();	
	}
	
	void print_periodic_stats(int interval = 1000)
	{
		while(is_running.load(std::memory_order_acquire))
		{
			// wait for stats to collect
			std::this_thread::sleep_for(std::chrono::milliseconds(interval));
			
			// take snapshot
			mtx_.lock();
			FrameStats snapshot(windowed_stats_);
			windowed_stats_.reset_window(snapshot.last_seq_);
			mtx_.unlock();
			
			//print the snapshot stats
			snapshot.print_stats();
				
		}
	}
	
	void start_print_stats(int interval = 1000)
	{
		logger_thread = std::thread(&PipelineStats::print_periodic_stats,
						this,interval);
	}
	
	void stop_print_stats()
	{
		is_running.store(false,std::memory_order_release);
		if (logger_thread.joinable())
			logger_thread.join();		
	}
	
	~PipelineStats()
	{
		stop_print_stats();
		print_lifetime_stats();
	}
};
