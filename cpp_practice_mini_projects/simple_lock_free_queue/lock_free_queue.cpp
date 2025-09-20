#include <iostream>
#include <thread>
#include <chrono>
#include "lock_free_queue.h"

int main()
{
    size_t num_elements = 30;
    lockFree_spsc_Queue<int> Q(num_elements);
    std::thread producer( [&](){
        for(int i=0;i<num_elements;i++)
        {
            while(Q.full()) {}
            Q.push(i);
            std::cout << "Producer push: " << i << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } );

        std::thread consumer( [&](){
        int ret;
        for(int i=0;i<num_elements;i++)
        {
            while(Q.empty()) {}
            ret = Q.pop();
            std::cout << "Consumer pop: " << ret << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } );

    producer.join();
    consumer.join();
}