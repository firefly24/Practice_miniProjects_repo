#pragma once

#include <opencv2/opencv.hpp>
#include <chrono>
#include <cstdint>

/**
* INVARIANTS
* 1. Frame should be immutable by design
* 2. Frame will be shared pointer through the participants, for life-cycle management
*/

using TimeStamp = std::chrono::steady_clock::time_point; 

class Frame{

private:
	cv::Mat img_;
	TimeStamp timestamp_;
	unsigned int seq_;
	
public:
	Frame(cv::Mat img, TimeStamp ts, unsigned int seq)
	: img_(std::move(img)), timestamp_(ts), seq_(seq) {}
	
	const cv::Mat& get_image() const { return img_; }
	TimeStamp get_timestamp() const {return timestamp_;}
	unsigned int get_seq() const { return seq_; }
};
