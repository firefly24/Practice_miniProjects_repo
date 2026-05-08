#pragma once

#include <atomic>
#include <memory>
#include "frame.h"

/**
*Notes:
*pass by value: I take ownership
*pass by reference: I borrow temporarily
*
*/

class FrameSharedState {

private:
	// actual shared data between producer and consumer
	std::shared_ptr<Frame> frame_{};
public:
	void publish(std::shared_ptr<Frame> frame)
	{
		// Publishes the latest frame ready for consumption
		/*
		The frame shareptr is passed by value instead of reference, as then publish will have its own copy of the sharedptr, which will increment its refcount, which shows ownership transfer. Whereas if we pass sharedptr reference, publish will hold reference to original sharedptr but not have its own copy, and refount is not actually increased in this case. This blurrs the ownership boundary as the producer still holding the orignal reference has the power to modify frame data being consumed by consumer.
		*/
		std::atomic_store_explicit(&frame_,
					std::move(frame),
					 std::memory_order_release);
	}

	std::shared_ptr<Frame> get_latest_frame()
	{
		// returns the pointer to the latest available frame
		return std::atomic_load_explicit(&frame_,std::memory_order_acquire);
	
	}
};
