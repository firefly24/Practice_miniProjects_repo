#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <vector>

using namespace std;

static std::mutex cout_mtx;
typedef std::function<void()> T;

//template <typename T>
class ThreadPool_Q {
private:
    std::size_t capacity;
    std::size_t maxWorkers;
    std::queue<T> taskList;
    std::mutex mtx;
    std::condition_variable cond_;
    std::vector<std::thread> worker_threads;
    unsigned int completed_tasks;
    bool stop_pool;

    // Disable copying
    ThreadPool_Q(const ThreadPool_Q&) = delete;
    ThreadPool_Q& operator=(const ThreadPool_Q&) = delete;

    // Disable moving
    ThreadPool_Q(ThreadPool_Q&&) = delete;
    ThreadPool_Q& operator=(ThreadPool_Q&&) = delete;

    // Worker thread function to pop tasks from the queue and execute them
    void startWorkerThread()
    {
        T task;
        // keep polling for new tasks on this thread
        while(1)
        {
            unique_lock<mutex> mLock(mtx);
            // Wait until there is a task in the queue
            cond_.wait(mLock, [this](){return !taskList.empty() ||  stop_pool;});

            //return only if pool is stopped and all tasks are completed
            if (stop_pool && taskList.empty())
                return;

            // TODO : Pop a task from the queue to attach to current worker thread
            task = std::move (taskList.front());
            taskList.pop();
            mLock.unlock();
            // TODO : Execute the task 
            task();
            // Notify that a task has been completed
            lock_guard<mutex>  lock(cout_mtx);
            completed_tasks++;
            cout << "Worker thread: " << std::this_thread::get_id() << " completed task:"<< completed_tasks << endl;

        }
    }

public:

    // Constructor launch worker threads
    explicit ThreadPool_Q(std::size_t task_capacity, std::size_t max_workers) : capacity(task_capacity) ,maxWorkers(max_workers)
    {
        completed_tasks = 0;
        stop_pool = false;
        // Initialize worker threads
        for(int i=0;i<maxWorkers;i++)
            worker_threads.emplace_back([this]{startWorkerThread();}); // lamba fn to pop a task from queue to execute
    }

    // Push a task into the queue
    //void pushTask(T task)
    template<typename Func, typename... Args>
    void pushTask(Func&& func,Args&&... args)
    {
        std::unique_lock<std::mutex> mLock(mtx);

        // We need to encapsulate the function such that calling func() will be equivalent to calling func(args...)
        // To do this, we can bind the args.. to func object by: auto task =  std::bind(func,args...);
        // But, we need to preserve the lvalue/rvalue typeof arguments, so we need to use perfect forwarding
        auto task = std::bind(std::forward<Func>(func),std::forward<Args>(args)...);

        // Wait until there is space in the queue
        cond_.wait(mLock, [this]() {return (taskList.size() < capacity) || stop_pool ;} );

        // If pool is stopped, do no not push any tasks to the queue
        if (stop_pool) 
            return;
        taskList.push([task](){task();});
        mLock.unlock();
        cond_.notify_one();
    }

    void stopPool(){
        std::unique_lock<std::mutex> mLock(mtx);
        stop_pool = true;
        mLock.unlock();
        cond_.notify_all(); // Notify all worker threads to wake up and check the stop condition
    }

    ~ThreadPool_Q()
    {
        // Stop the pool and notify all worker threads
        stopPool(); 
        // Join all worker threads
        for (auto& worker : worker_threads)
            worker.join();
    }
};