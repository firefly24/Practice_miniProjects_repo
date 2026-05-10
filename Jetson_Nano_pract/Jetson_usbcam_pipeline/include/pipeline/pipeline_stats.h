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

private:
	unsigned int frame_count_;
	int dropped_frames_;
	unsigned int null_frames_;
	unsigned int read_failures_;
	int last_seq_;
	double fps_;
	double sum_latency_ms_;
	double max_latency_ms_;
	Timepoint start_time_;
	
public:
	FrameStats(): frame_count_(0),dropped_frames_(0),
			null_frames_(0), read_failures_(0),last_seq_(-1),
			fps_(0.0), sum_latency_ms_(0.0),max_latency_ms_(0.0),
			start_time_(std::chrono::steady_clock::now()) {}
					 
	void reset_window(int last_seen_seq=-1)
	{
		frame_count_	=0;
		dropped_frames_	=0;
		null_frames_	=0;	
		read_failures_	=0;
		last_seq_	=last_seen_seq;
		fps_		=0.0;
		max_latency_ms_	=0.0;
		sum_latency_ms_	=0.0;
		start_time_	= std::chrono::steady_clock::now();
	}
	
	double elapsed_time_ms() const
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>
			(std::chrono::steady_clock::now() - start_time_).count();
	}
	
	void record_frame(int seq, double latency_ms)
	{
		//add dropped frame count from last displayed frame seq
		dropped_frames_ += seq- last_seq_ -1 ;
		
		// update max capture->display latency
		max_latency_ms_ = std::max(max_latency_ms_,latency_ms);
		
		// add latency to be used to calculate avg later
		sum_latency_ms_ += latency_ms;
		
		// update the latest seen frame seq
		last_seq_ = seq;
		
		// update total frames recorded
		frame_count_++;
	}
	
	void record_null_frame()
	{
		null_frames_++;
	}
	
	void record_read_failure()
	{
		read_failures_++;
	}	
	
	int get_last_seq()
	{
		return last_seq_;
	}
	
	void print_stats_readable()
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
			<< " avg latency:" << (sum_latency_ms_/frame_count_)
			<< "ms max latency:" << max_latency_ms_
			<< "ms dropped frames:" << dropped_frames_
			<< " null_frames:" << null_frames_
			<< std::endl;
	}
	
	void print_stats_csv(std::ostream& out) 
	{
	
		//if (!out.is_open())	return;
			
		fps_ = frame_count_ / (elapsed_time_ms()/1000.0);
		
		
		out << elapsed_time_ms() << ","
		 << frame_count_ << ","
		 << fps_ << ","
		 << ((frame_count_)?(sum_latency_ms_/frame_count_):0.0) << ","
		 << max_latency_ms_ << ","
		 << dropped_frames_ << ","
		 << null_frames_ << ","
		 << read_failures_ << "\n";
		 
		 out.flush();
	}
};

class PipelineStats{

private:

	FrameStats windowed_stats_;
	FrameStats lifetime_stats_;
	std::fstream stats_file_;
	std::string filename_;
	bool csv_enabled_;
	
	
	std::atomic<bool> is_running{false};
	std::thread logger_thread;
	std::mutex mtx_;
	
public:

	PipelineStats(std::string filename = ""):filename_(filename)
	{
		is_running.store(true, std::memory_order_release);
		
		if (filename == "")
		{
			csv_enabled_ = false;
			return;
		}
		
		stats_file_.open(filename_, std::ios::out | std::ios::trunc);
		if (stats_file_.is_open())
		{
			stats_file_ << "window_ms,frames,fps,avg_latency_ms,max_latency_ms,"
                       "dropped_frames,null_frames,read_failures\n";
                       stats_file_.flush();
                       csv_enabled_  = true;
		}
		else {
			csv_enabled_ = false;
			std::cout << "Failed to open stats file: " << filename_ << std::endl;
		}
	}
	
	// Consumer must record every frame in the stats
	void record_frame(const Timepoint &capture_time,
				const Timepoint display_time,const int& seq)
	{
		
		// log lifetime stats now
		// give console output for now, log in csv later
		auto duration = std::chrono::duration<double, std::milli>
		(display_time - capture_time).count();

		mtx_.lock();
		lifetime_stats_.record_frame(seq,duration);
		windowed_stats_.record_frame(seq,duration);
		mtx_.unlock();
	}
	
	void record_null_frame()
	{
		mtx_.lock();
		lifetime_stats_.record_null_frame();
		windowed_stats_.record_null_frame();
		mtx_.unlock();
	}
	
	void record_read_failure()
	{
		mtx_.lock();
		lifetime_stats_.record_read_failure();
		windowed_stats_.record_read_failure();
		mtx_.unlock();
	}
	
	void print_lifetime_stats()
	{
		mtx_.lock();
		FrameStats snapshot(lifetime_stats_);
		mtx_.unlock();
		snapshot.print_stats_readable();	
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
			windowed_stats_.reset_window(snapshot.get_last_seq());
			mtx_.unlock();
			
			//print the snapshot stats
			if(csv_enabled_)
				snapshot.print_stats_csv(stats_file_);
			else
				snapshot.print_stats_readable();
				
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
