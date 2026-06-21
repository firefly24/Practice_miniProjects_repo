#pragma once

#include <opencv2/opencv.hpp>
#include <iostream>
#include "pipeline/stats_mode.h"


class CaptureSource {

private:
	cv::VideoCapture cam_capture_;
	int capture_width_;
	int capture_height_;

public:
	bool open(int device_id=0) /*: cam_capture_(device_id)*/
	{
		cam_capture_ = cv::VideoCapture(device_id,cv::CAP_V4L2);
		if(!cam_capture_.isOpened())
		{
			std::cout << "Camera feed open failed for device id:"
				<< device_id << std::endl;
			return false;
		}	
		capture_width_ = static_cast<int>(cam_capture_.get(cv::CAP_PROP_FRAME_WIDTH));
		capture_height_ = static_cast<int>(cam_capture_.get(cv::CAP_PROP_FRAME_HEIGHT));
		return true;
	}
	
	bool read(cv::Mat& captured_frame)
	{
		nvtx3::scoped_range r{"CameraCapture"};
		if (!cam_capture_.isOpened())
		{
			std::cout << "Camera device failed open!" << std::endl;
			return false;
		}
		
		return cam_capture_.read(captured_frame);

	}
	
	void close()
	{
		if (cam_capture_.isOpened())
			cam_capture_.release();
			
		std::cout << "capture width: " << capture_width_
			<< " height: "<< capture_height_ << std::endl;
	}
	
	~CaptureSource()
	{
		close();
	}
};

