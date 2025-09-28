#include <iostream>
#include <cassert>
#include <chrono>
#include "mpmc_queue_bounded.h"

std::vector<int> producer_push_success_count;
std::vector<int> consumer_pop_success_count;

void enqueue(mpmcQueueBounded<int> &Q, int producer_num, int num_elements)
{
    for (int i=0;i<num_elements;i++)
    {
        //if (Q.try_push(producer_num))
        while(!Q.try_push(producer_num)){}
            producer_push_success_count[producer_num]++;
    }
}

void dequeue (mpmcQueueBounded<int> &Q, int consumer_num, int num_elements)
{
    int out;
    for (int i=0;i<num_elements;i++)
    {
        //if (Q.try_pop(out))
        while(!Q.try_pop(out)){}
            consumer_pop_success_count[consumer_num]++;
    }
}

int main()
{
    size_t capacity = 1<<16;
    int num_actors = 5;
    int num_elements = 1000000;
    int total_elems = num_elements * num_actors;

    std::vector<std::thread> producer;
    std::vector<std::thread> consumer;

    producer_push_success_count = std::vector<int>(num_actors,0);
    consumer_pop_success_count = std::vector<int>(num_actors,0);

    mpmcQueueBounded<int> Q(capacity);

    /*
    for (int i=0;i<capacity;i++)
        assert(Q.buffer_[i].seq.load(std::memory_order_relaxed) == i);
    */

    auto start = std::chrono::high_resolution_clock::now();

    for (int i=0;i<num_actors;i++)
    {
        producer.emplace_back([&Q, i, num_elements](){ enqueue(Q,i, num_elements); } ); 
    }

    for (int i=0;i<num_actors;i++)
    {
        consumer.emplace_back([&Q,i,num_elements](){ dequeue(Q,i,num_elements); } );
    }

    for(int i=0;i<num_actors;i++)
        producer[i].join();
    for(int i=0;i<num_actors;i++)
        consumer[i].join();

    auto end = std::chrono::high_resolution_clock::now();

    auto time_taken = std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();

    double throughput = ((double)total_elems) / (time_taken / 1000.0);

    std::cout << "Elements:" << total_elems << "\nTime taken: " << time_taken << "ms\nThroughput: " << throughput << std::endl;

    for (int i=0;i<num_actors;i++)
    {
        std::cout << "Producer success: " << producer_push_success_count[i] 
                    << "\t consumer_success: " << consumer_pop_success_count[i] << std::endl;
    }

    return 0;
}