Implemented a lock free bounded queue based on Dimitry Vyukow style queue design, to learn and understand lock-free concurrency using atomics and memory ordering principles and compare the performance with a queue with locks.

Learned cache line alignment and false sharing, and its impact on performance.

Learned the importance of std::launder while derefencing pointers casted form placement new.

For performance analysis, I have temporarily given up on trying any profiling on jetson orin nano board, as none of the profiling tools like perf, pprof, google-pprof working on this board. 
Seems Nsight systems is the only option, but it requires to re-flash of the board with jetpack SDK manager, which I might do later as re-setup of board is quite tedious.