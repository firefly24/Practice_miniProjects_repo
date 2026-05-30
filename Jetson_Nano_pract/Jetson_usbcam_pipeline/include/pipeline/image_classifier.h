#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include "../../models/labelsimagenet1k.h"
#include "stats_mode.h"


struct Prediction{
	int class_id;
	float confidence;
	std::string label;
	
	Prediction(){}
	
	Prediction(int class_id_, float conf)
	{
		class_id = class_id_;
		confidence = conf;
		label = "";
	}
};


class ImageClassifier{

private:
	cv::dnn::Net net_;
	std::vector<std::string> labels_;
	
	
	cv::Mat preprocess(const cv::Mat& img) const
	{
		nvtx3::scoped_range p{"PreProcess"};
		cv::Mat blob = cv::dnn::blobFromImage(img,
						1.0/255.0, 
						cv::Size(224,224),
						cv::Scalar(),true,false);
						
		return blob;
	}

public:

	ImageClassifier(const std::string& model_path/*,
			const std::string& labels_path*/)
	{
		try 
		{
			net_ = cv::dnn::readNetFromONNX(model_path);
		}
		catch(const cv::Exception &e)
		{
			std::cerr << "Failed to load model: " << e.what() <<std::endl;
			throw;
		}
		
		// TODO: how to load label names from label path instead of specific header file
		labels_ = getLabelsImagenet1k();
		
	}

	Prediction infer(const cv::Mat& img)
	{
		if(img.empty())
		{
			throw std::runtime_error("infer() received an empty image");
		}
		
		cv::Mat prep_blob = preprocess(img);
		
		nvtx3::scoped_range i{"Infer"};
		// set input and run inference
		net_.setInput(prep_blob);
		cv::Mat prob = net_.forward();
		
		/* decode output find class with highest probability*/
		double maxProb =0.0;
		cv::Point classIdPoint;
		cv::minMaxLoc(prob.reshape(1,1),0,&maxProb,0,&classIdPoint);
		
		Prediction ans(classIdPoint.x, maxProb);
		
		if( ans.class_id >=0 && ans.class_id < static_cast<int>(labels_.size()) )
			ans.label = labels_[classIdPoint.x];
		else
			ans.label = "unknown";
		
		return ans;
	}
	
	
	void drawPrediction(cv::Mat &img, const Prediction &pred)
	{
		nvtx3::scoped_range d{"DrawOverlay"};
		std::string text = pred.label;
		
		text += "score: " + cv::format("%.2f",pred.confidence);
		
		int baseline = 0;
		cv::Size text_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX,
							0.8,2,&baseline);
							
		// Background box 
		cv::rectangle (img, cv::Point(10,10),
				cv::Point(10+text_size.width +12 , 10+ text_size.height+12),
				cv::Scalar(0,0,0),
				cv::FILLED);
				
		// text
		cv::putText(img, text, cv::Point(16,10+text_size.height +2),
				cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0,255,0),2);
	
	}
};





























