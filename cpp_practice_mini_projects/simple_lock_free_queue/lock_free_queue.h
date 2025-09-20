/***
 *  Implementing a simple lock-free queue , for Single Producer Single Consumer use-case
 *  Assumptions: 
 *      1. The items in the queue will be homogenous
 *      2. The capacity of the queue is bounded, and is decided at the time of queue initialization
 *      3. The queue will be accessed by a single producer and single consumer only
 *  
 *  Noting down implementation ideas:
 *      1. Using a vector to simulate a circular ring buffer of size of queue capacity
 *      2. use q_size to determine if ring_buffer is full or empty, but should q_size also be atomic ? 
 *      3. have 2 indices - head and tail. Both should be atomic
 *                          Producer will insert the item at the tail index (back of queue)
 *                          Consumer will pop an item from the head index (front of queue)
 *      4. For single producer, single consumer problem, atomicity of head and tail will probably help only when in case there is only one element in the queue
 *          meaning head == tail, which will produce inconsistant results if kept non-atomic
 *
 *  Notes from resources on std::atomic:
 *      * atomic is used in lock-free concurrency programming, also for non-blocking operations which are free from data-races 
 *        meaning thread-safety is ensured for shared variables in concurrent scenarios, without the use of explict locking like locks and mutexes
 *      * There is a probability that internally the atomic operations may be implementated with locks ( need to confirm )
 *      * To check if an atomic operation is truly lock-free, std::atomic::is_lock_free() can be used
 *      * key atomic operations to remember for now -> load(), store(), compare_and_exchange(), ++/-- etc
 *      * Need to pay special attention to memory ordering in the atomic operations
 *      * In short, by default, the order of execution is strictly same as the order of instruction written in code
 *      * std::memory_order_relaxed is where the compiler is free to do optimizations by changing the order of instructions
 *      * std::memory_
 * 
 *  Videoes I referenced for understanding std::atomic, memory ordering and memory barriers:
 *      https://youtu.be/ZQFzMfHIxng?si=OtfwP7XMSlboYxb2
 *      https://youtu.be/IE6EpkT7cJ4?si=3A85Lp0waSTTnEzK (more visual explanations)
 * 
 */


#include <cstddef>
#include <atomic>
#include <vector>
#include <iostream>

template <typename T>
class lockFree_spsc_Queue
{
private:
    size_t capacity;
    std::vector<T> ring_buff;
    std::atomic<size_t> head; // Index of first item
    std::atomic<size_t> tail; // Index of the next empty slot after last item


    // This function helps to increment the head/tail index such that it wraps around the ring buffer
    size_t advance(size_t index) const
    {
        return (index+1)%capacity; // while advancing index, wrap around the ring buffer
    }

public:

    explicit lockFree_spsc_Queue (size_t Capacity): head(0), tail(0)
    {
        capacity = Capacity;
        ring_buff = std::vector<T>(capacity+1); // keep one empty element between head and tail to check if buffer full/empty
    }

    lockFree_spsc_Queue(const lockFree_spsc_Queue&) = delete;
    lockFree_spsc_Queue& operator=(const lockFree_spsc_Queue&) = delete;

    // TODO : Implement push move version, and an emplace version 

    // Push item by Producer to the end of the queue
    //copy version
    void push(const T& item)
    {
        // Only producer thread exclusively modifies the tail of the buffer, whereas consumer thread has no reason to modify tail.
        // So we can load tail idx in relaxed order in producer thread

        size_t back = tail.load(std::memory_order_relaxed);
        size_t next;

        // Check if queue is full
        // Since head can be modified by consumer thread, we would want the consumer to finish updating the head before publishing it to producer(this) 
        // So we will use acquire ordering for head to ensure getting an up-to-date head 
        if ((next = advance(back)) == head.load(std::memory_order_acquire))
        {
            std::cout << "Queue is full" << std::endl;
            return;
        }
        ring_buff[back] = item;
        // Producer(this) thread need to publish the incremented tail, only after the item is inserted to the queue,
        // so ordering is important here, and so we use release ordering to publish new tail only after above op is done
        tail.store(next,std::memory_order_release);  
    }

    // Remove/consume item by consumer thread from the front of the queue
    //TODO : fix return value on empty queue
    T pop()
    {
        // Consumer thread will only consume from the head of the queue, so it will advance only head idx and not modify tail idx
        size_t front = head.load(std::memory_order_relaxed);

        // Check if queue is empty
        //we want to access the tail under the condition that the producer is done pushing new element and incrementing the tail
        // so we need to impose an order here as well, that consumer views the updated tail published by consumer
        if (front == tail.load(std::memory_order_acquire))
        {
            std::cout << "Queue is empty" << std::endl;
            return NULL;
        }
        T ret = ring_buff[front];
        head.store(advance(front), std::memory_order_release);
        // published the updated head after consuming front element
        return ret;
    }

    bool empty() const {
        return (head.load(std::memory_order_acquire) == tail.load(std::memory_order_acquire));
    }

    bool full() const {
        size_t next = advance(tail.load(std::memory_order_acquire));
        return next == head.load(std::memory_order_acquire);
    }
};