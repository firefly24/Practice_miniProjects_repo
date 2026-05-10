#pragma once

#include <opencv2/opencv.hpp>
#include <iostream>


class CaptureSource {

private:
	cv::VideoCapture cam_capture_;
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
		return true;
	}
	
	bool read(cv::Mat& captured_frame)
	{
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
	}
	
	~CaptureSource()
	{
		close();
	}
};

