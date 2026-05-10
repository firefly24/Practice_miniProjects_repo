#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include "../include/pipeline/producer.h"
#include "../include/pipeline/consumer.h"
#include "../include/pipeline/stats_mode.h"

using namespace std;

int main()
{
	/*
	int frame_width = static_cast<int>(cam_capture.get(cv::CAP_PROP_FRAME_WIDTH));
	int frame_height = static_cast<int>(cam_capture.get(cv::CAP_PROP_FRAME_HEIGHT));
	
	cout << "frame widthxheight: " << frame_width << "x" << frame_height << endl;
	*/
	
	
	FrameSharedState latest_frame;
	std::atomic<bool> running{true};
	int device_id =0;
	int screen_refresh_interval_ms = 20;
	
	//running.store(true,std::memory_order_release);
	StatsType stats = StatsType("recorded_frame_stats.csv");
	//StatsType stats;
	
	Producer cam_feed(latest_frame,running,stats,device_id);
	Consumer display_feed(latest_frame,running,stats,screen_refresh_interval_ms);
	
	stats.start_print_stats();
	
	std::thread producer_thread(&Producer::run, &cam_feed);
	std::thread consumer_thread(&Consumer::run, & display_feed);

	producer_thread.join();
	consumer_thread.join();
	
	stats.stop_print_stats();
	cv::destroyAllWindows();
	
	return 0;
}
