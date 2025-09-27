/***
 *  Implementing a simple mpmc queue without mutexes and locks
 */
#include <vector>
#include <mutex>
#include <utility>
#include <condition_variable>

//using namespace std;

template <typename T>
struct Cell{
    std::atomic<size_t> seq;
    //T* mem;
    // important, as we don't know  the number of bytes used by aligned T object in memory, as some padding might be used too
    alignas(alignof(T)) char mem[sizeof(T)];
    /*Cell()
    {
        mem = operator new(sizeof(T));
    }
    ~Cell()
    {
        if (mem != nullptr)
            operator delete(mem);
    }
    */
};

static size_t get_ub_size(size_t cap)
{
    // This function is used to calculate the size of the buffer of our bounded queue, based on the capacity requested;
    // We try to set the queue size the to the closest upper bound of power of 2, from the capacity requested;
    size_t i=0;
    if ((cap & (cap-1)) == 0 )
        return cap;
    while(cap)
    {
        cap = cap >> 1;
        i++;
    }
    return 1<<i;
    // TODO: what if the size is already closer to UINT_MAX, there is no upper bound for that
}

template <typename T>
class mpmcQueueBounded
{

private:
    // avoiding false sharing
    alignas(64) std::atomic<size_t> enq_tail{0};
    alignas(64) std::atomic<size_t> deq_head{0};
    size_t capacity_;
    size_t mask_; // For size related operations 

public:
    std::vector<Cell<T>> buffer_;
    mpmcQueueBounded(size_t capacity) : capacity_(get_ub_size(capacity)),
                                        mask_(capacity_ -1),
                                        buffer_(capacity_)
    {
        for (size_t i=0;i<capacity_;i++)
            buffer_[i].seq.store(i,std::memory_order_relaxed);
    }

    // for copy item type
    bool try_push(T item)
    {
        size_t tail = enq_tail.load(std::memory_order_relaxed);
        
        size_t slot_idx = tail & mask_; // increment the buffer_idx such that it wraps around the ring buffer
        
        if (buffer_[slot_idx].seq.load(std::memory_order_acquire) != tail)
            return false;   // this slot has not been freed by the consumer yet, return queue is full

        if (!enq_tail.compare_exchange_strong(tail,tail+1,std::memory_order_acquire,std::memory_order_relaxed))
            return false;

        T *data = reinterpret_cast<T*>(&(buffer_[slot_idx].mem));
        new (data) T(item); // This will probably invoke copy constructor of T

        // only after item is constructed in-memory , should we publish the new seq no, so release ordering required;
        buffer_[slot_idx].seq.store(tail+1,std::memory_order_release);
        return true;
    }


    // for move-only item type
    bool try_push(T&& item)
    {
        size_t tail = enq_tail.load(std::memory_order_relaxed);
        size_t slot_idx = tail & mask_; // increment the buffer_idx such that it wraps around the ring buffer

        if (buffer_[slot_idx].seq.load(std::memory_order_acquire) != tail)
            return false; // this slot has not been freed by the consumer yet, return queue is full

        if (!enq_tail.compare_exchange_strong(tail,tail+1,std::memory_order_acquire,std::memory_order_relaxed))
            return false;

        T *data = reinterpret_cast<T*>(&(buffer_[slot_idx].mem));
        new (data) T(std::move(item)); // This will probably invoke move constructor of T

        // only after item is constructed in-memory , should we publish the new seq no, so release ordering required;
        buffer_[slot_idx].seq.store(tail+1,std::memory_order_release);
        return true;
    }

    //emplace version
    template <typename... Args>
    bool try_emplace(Args&&... args)
    {
        size_t tail = enq_tail.load(std::memory_order_relaxed);
        size_t slot_idx = tail & mask_;

        if (buffer_[slot_idx].seq.load(std::memory_order_acquire) != tail)
            return false;   // this slot has not been freed by the consumer yet, return queue is full

        if (!enq_tail.compare_exchange_strong(tail,tail+1,std::memory_order_acquire,std::memory_order_relaxed))
            return false;

        // increment the buffer_idx such that it wraps around the ring buffer

        T *data = reinterpret_cast<T*>(&(buffer_[slot_idx].mem));
        new (data) T(std::forward<Args>(args)...); // This will probably invoke move constructor of T

        // only after item is constructed in-memory , should we publish the new seq no, so release ordering required;
        buffer_[slot_idx].seq.store(tail+1,std::memory_order_release);
        return true;
    }

    // Pop an item from the queue, and mark buffer as empty for producer
    bool try_pop(T &out)
    {
        size_t head = deq_head.load(std::memory_order_relaxed);
        size_t slot_idx = head & mask_;

        if (buffer_[slot_idx].seq.load(std::memory_order_acquire) != (head+1))
            return false;   // We must first check if the slot is free, if buffer slot is not free, doesn't make sense to CAS the head

        if (!deq_head.compare_exchange_strong(head,head+1,std::memory_order_acquire,std::memory_order_relaxed))
            return false;

        // seq == head+1 means the slot is ready with data prepared by producer
        // We move the ownership of the data in the buffer slot, to an out variable, which our consumer thread can perform work on later
        T *data = reinterpret_cast<T*>(&(buffer_[slot_idx].mem));
        out = std::move(*data);
        data->~T();

        buffer_[slot_idx].seq.store(head+capacity_,std::memory_order_release);
        return true;
    }

};