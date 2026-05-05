cmake_minimum_required(VERSION 3.10)
project(JetsonCamPipeline)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenCV REQUIRED)

include_directories(${OpenCV_INCLUDE_DIRS})

add_executable(cam_app
	src/main.cpp
)

target_link_libraries(cam_app ${OpenCV_LIBS})
