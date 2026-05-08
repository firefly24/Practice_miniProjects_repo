#include <opencv2/opencv.hpp>
#include <iostream>
#include <thread>
#include "../include/capture_source.h"
#include "../include/pipeline/producer.h"

using namespace std;

int main()
{
	/*
	int frame_width = static_cast<int>(cam_capture.get(cv::CAP_PROP_FRAME_WIDTH));
	int frame_height = static_cast<int>(cam_capture.get(cv::CAP_PROP_FRAME_HEIGHT));
	
	cout << "frame widthxheight: " << frame_width << "x" << frame_height << endl;
	*/
	
	
	FrameSharedState latest_frame;
	std::atomic<bool> running;
	int device_id =0;
	
	running.store(true,std::memory_order_release);
	
	Producer cam_feed(latest_frame,running,device_id);
	
	std::thread producer_thread([&cam_feed](){ cam_feed.run(); });

	while(true)
	{
		
		std::shared_ptr<Frame> process_frame =	latest_frame.get_latest_frame();
		
		if (!process_frame)
		{
			std::this_thread::sleep_for(
					std::chrono::milliseconds(5));
			continue;
		}
			
		cv::imshow("Camera feed",process_frame->get_image());
		
		int key = cv::waitKey(20);
		
		if (key == 'q')
		{
			cout << "Key q is pressed, quitting stream" <<endl;
			running.store(false, std::memory_order_release);
			break;
		}
	}
	producer_thread.join();
	cv::destroyAllWindows();
	
	return 0;
}
