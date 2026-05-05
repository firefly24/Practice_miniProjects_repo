#include <opencv2/opencv.hpp>
#include <iostream>

using namespace std;

int main()
{
	cv::VideoCapture cam_capture(0);
	
	if (!cam_capture.isOpened())
	{
		cout << "Error opening camera stream" << endl;
		return 0;
	}
	
	int frame_width = static_cast<int>(cam_capture.get(cv::CAP_PROP_FRAME_WIDTH));
	int frame_height = static_cast<int>(cam_capture.get(cv::CAP_PROP_FRAME_HEIGHT));
	
	cout << "frame widthxheight: " << frame_width << "x" << frame_height << endl;
	
	while(cam_capture.isOpened())
	{
		cv::Mat img;
		
		bool is_success = cam_capture.read(img);
		
		if(!is_success)
		{
			cout << "Camera read data failed" << endl;
			break;
		}
		
		cv::imshow("Camera feed",img);
		
		int key = cv::waitKey(20);
		
		if (key == 'q')
		{
			cout << "Key q is pressed, quitting stream" <<endl;
			break;
		}
	}
	cam_capture.release();
	cv::destroyAllWindows();
	
	return 0;
}
