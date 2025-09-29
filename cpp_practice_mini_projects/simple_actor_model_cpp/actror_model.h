#include <cstddef>
#include <thread>
#include <functional>
#include "../simple_mpmc_queue/mpmc_queue_bounded.h"

template <typename Task>
class Actor
{

private:
    size_t id_;
    mpmcQueueBounded<Task> *mailbox_q;
public:
    void receive()
    {
        while(!mailbox_q.try_pop())
        {
            // Will this mpmc queue work for std::function<void()> ?  
        }
    }

};