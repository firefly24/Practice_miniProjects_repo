#include <cstddef>
#include <atomic>
#include <queue>

template <typename T>
class lockFree_Queue
{

private:
    size_t capacity;
    size_t q_size;
    std::queue<T> Q;

public:
    size_t size() const {
        return q_size;
    }

    bool empty() const {
        // TODO : this won't be atomic, do we check q.empty() or q size ?
        return Q.empty();
    }

    T front() const {
        // Undefined behavior on calling front() on empty queue
        // TODO:  Handle for empty queue scenario
        return Q.front();
    }

    T back() const {
        // Undefined behavior on calling back() on empty queue
        // TODO:  Handle for empty queue scenario
        return Q.back();
    }

    void push(const T& item)
    {
        
    }



};