/***
 *  Implementing a simple mpmc queue without mutexes and locks
 */
#include <vector>
#include <utility>
#include <thread>
#include <atomic>
#include <cstddef>
#include <new>

//using namespace std;

template <typename T>
struct Cell{

    //sequence num of slot, which will be modified by producer and consumer threads to indicate slot empty/full
    // For picking the index of a slot in the queue ring buffer, we use head%capacity or tail%capacity
    /* For example, tail = 18, capacity = 16,
     to find the slot in queue's ring buffer where cell.seq == tail, 
     means to find index where buffer_[index].seq == tail
     we keep our index and seq related such that:
        index = tail%capacity
        so buffer[2].seq could be equal to tail

    */
    /* LOGIC OF SEQ FOR PRODUCER
        - For writing data, a producer picks a cell(slot) where the cell.seq == current(tail)
        - it writes the data to this cell.mem, and increments the cell.seq = tail+1
        - So if (cell.seq - idx)%capacity == 1, IT MEANS A FILLED CELL
        - whereas if (cell.seq - idx)%capacity == 0, means an empty cell
    */
       /* LOGIC OF SEQ FOR CONSUMER
        - For consuming data, a consumer picks a cell(slot) where the cell.seq == current(head)+1 ->indicates filled cell
        - whereas 
        - it writes the data to this cell.mem, and increments the cell.seq = head+capacity
        - So if (cell.seq - idx)%capacity == 0, IT MEANS AN EMPTY CELL
    */
    std::atomic<size_t> seq;
    // important, as we don't know  the number of bytes used by aligned T object in memory, as some padding might be used too
    alignas(alignof(T)) char mem[sizeof(T)];
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


// Important note: T must me noexcept, so that there is no exception raised by T constructor in placement new 

template <typename T>
class mpmcQueueBounded
{

private:
    // avoiding false sharing on cache line
    alignas(64) std::atomic<size_t> enq_tail{0};
    alignas(64) std::atomic<size_t> deq_head{0};
    size_t capacity_;
    size_t mask_; // For size related operations 
    std::vector<Cell<T>> buffer_;

    bool hasActiveData(size_t seq, size_t slotno)
    {
        return ((seq-slotno)%capacity_) == 1 ;
    }

public:
    explicit mpmcQueueBounded(size_t capacity) : capacity_(get_ub_size(capacity)),
                                        mask_(capacity_ -1),
                                        buffer_(capacity_)
    {
        for (size_t i=0;i<capacity_;i++)
            buffer_[i].seq.store(i,std::memory_order_relaxed);
    }

    // Delete copy constructor
    mpmcQueueBounded(const mpmcQueueBounded&) = delete;
    mpmcQueueBounded operator=(const mpmcQueueBounded&) = delete;

    // for copy item type
    bool try_push(T item)
    {
        int count = 0;
        while(1)
        {
            size_t tail = enq_tail.load(std::memory_order_relaxed);
            size_t slot_idx = tail & mask_;
            size_t idx = buffer_[slot_idx].seq.load(std::memory_order_acquire);

            if (idx == tail)    // if slot is empty
            {
                if (enq_tail.compare_exchange_weak(tail,tail+1,std::memory_order_relaxed,std::memory_order_relaxed))
                {
                    // Since tail increment is success, means we have successfully reserved a slot
                    // Now fill the data into the reserved slot 
                    T *data = std::launder(reinterpret_cast<T*>(&(buffer_[slot_idx].mem)));
                    new (data) T(item); // This will probably invoke move constructor of T, which is noexcept
                     // only after item is constructed in-memory , should we publish the new seq no, so release ordering required;
                    buffer_[slot_idx].seq.store(tail+1,std::memory_order_release);
                    return true;
                }
                continue;
            }
            if (idx < tail)
                return false; // Slot not freed by consumer yet
            
            count++;
            if (count > 16)
            {
                count = 0;
                std::this_thread::yield();  // seems this thread keeps spinning trying to reserve a slot,so we yield temporarily
            }   
        }
    }


    // for move-only item type
    bool try_push(T&& item)
    {
        int count = 0;
        while(1)
        {
            size_t tail = enq_tail.load(std::memory_order_relaxed);
            size_t slot_idx = tail & mask_;
            size_t idx = buffer_[slot_idx].seq.load(std::memory_order_acquire);

            if (idx == tail)
            {
                if (enq_tail.compare_exchange_weak(tail,tail+1,std::memory_order_relaxed,std::memory_order_relaxed))
                {
                    T *data = std::launder(reinterpret_cast<T*>(&(buffer_[slot_idx].mem)));
                    new (data) T(std::move(item)); // This will probably invoke move constructor of T
                     // only after item is constructed in-memory , should we publish the new seq no, so release ordering required;
                    buffer_[slot_idx].seq.store(tail+1,std::memory_order_release);
                    return true;
                }
                continue;
            }
            if (idx < tail)
                return false; // Slot not freed by consumer yet
            
            count++;
            if (count > 16)
            {
                count = 0;
                std::this_thread::yield();
            }   
        }
    }

    //emplace version
    template <typename... Args>
    bool try_emplace(Args&&... args)
    {
        int count = 0;
        while(1)
        {
            size_t tail = enq_tail.load(std::memory_order_relaxed);
            size_t slot_idx = tail & mask_;
            size_t idx = buffer_[slot_idx].seq.load(std::memory_order_acquire);

            if (idx == tail)
            {
                if (enq_tail.compare_exchange_weak(tail,tail+1,std::memory_order_relaxed,std::memory_order_relaxed))
                {
                    T *data = std::launder(reinterpret_cast<T*>(&(buffer_[slot_idx].mem)));
                    new (data) T(std::forward<Args>(args)...); // This will probably invoke move constructor of T
                     // only after item is constructed in-memory , should we publish the new seq no, so release ordering required;
                    buffer_[slot_idx].seq.store(tail+1,std::memory_order_release);
                    return true;
                }
                continue;
            }
            if (idx < tail)
                return false; // Slot not freed by consumer yet
            
            count++;
            if (count > 16)
            {
                count = 0;
                std::this_thread::yield();
            }   
        }
    }

    // Pop an item from the queue, and mark buffer as empty for producer
    bool try_pop(T &out)
    {
        int count = 0;
        while(1)
        {
            size_t head = deq_head.load(std::memory_order_relaxed);
            size_t slot_idx = head & mask_;
            size_t idx = buffer_[slot_idx].seq.load(std::memory_order_acquire);

            if (idx == (head+1))
            {
                if (deq_head.compare_exchange_weak(head,head+1,std::memory_order_relaxed,std::memory_order_relaxed))
                {
                    // Since atomic increment of head is successful, we have sucessfully reserved the slot mapping to old head

                    // seq == head+1 means the slot is ready with data prepared by producer
                    // We move the ownership of the data in the buffer slot, to an out variable, which our consumer thread can perform work on later
                    T *data = std::launder(reinterpret_cast<T*>(&(buffer_[slot_idx].mem)));
                    out = std::move(*data);
                    data->~T();
                    buffer_[slot_idx].seq.store(head+capacity_,std::memory_order_release);
                    return true;
                }
                continue;
            }
            if (idx <=head)
            {
                return false;   // We must first check if the slot is free, if buffer slot is not free, doesn't make sense to CAS the head
            }
            
            count++;
            if (count > 16)
            {
                count = 0;
                std::this_thread::yield();
            }   
        }
    }
    //destructor
    ~mpmcQueueBounded()
    {
        // We assume it is the producer and consumer responsibility to stop advancing head and tail before destructor is called
        for(size_t slot=0;slot<capacity_;slot++)
        {
            if(hasActiveData(buffer_[slot].seq.load(std::memory_order_relaxed), slot)) 
                std::launder(reinterpret_cast<T*>(&buffer_[slot].mem))->~T();
        }
    }

};