1. Simple Threadpool
   - Learned to use variadic templates in real life use-cases.
   - Created a threadpool which performs n number of tasks divided among a fixed number of threads, so that overhead of spawning a new thread for every task is avoided.
   - To maintain a queue of incoming tasks, used mutexes and condition_variables to avoid race conditions while popping tasks from task queue
   - Learned to use std::bind and std::forward to bind the task's function parameters to the task so that it can be pushed to the queue as a function pointer, and no need to store the parameters separately (perfect forwarding) (added comments in pushTask function for understanding)
   - use packaged_task and std::future for getting the return value of the tasks completed
