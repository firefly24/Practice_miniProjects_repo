Build a reliable and reusable simple telemetry data transport module that is based on simple producer-consumer design, which can be reused for sensors, stats collection, camera frames, DMA pipelines etc to deliver telemetry to userspace.


##Core Architecture:

Producer -> Telemetry module -> Buffering -> Synchronization -> Userspace consumer

##Submodules and their responsibilities
---


Ring Buffer - provides storage mechanism for generated records
Telemetry Device - owns operational policies


telemetry_main.c
    • Device lifecycle - char device init/exit 
    • Session management - each telemetry session starts with fops.open() and ends with fops.release()
    • Producer thread - pushes data to the telemetry data buffer
    • Character device operations - telemetry char device managment through fops

telemetry_ring.c
    • Generic SPSC ring buffer - has no idea about telemetry device lifecycle or policies
    • Push/Pop - Only performs push and pop operations, no synchronization or sleeping invloved
    • Capacity management - only checks if buffer full/empty/occupancy 

telemetry_stats.c
    • Statistics collection - collect buffer usage statistics for generated/consumed records
    • Historical metrics - collect metrics over a complete session from start to end
    • Reporting - report the metrics on need basis
    
---

## Updates: 

### Telemetry V3.4: Deferred implementation of backpressure policy DELETE_OLD

	For SPSC, when producer is faster than consumer, one of the possible backpressure policies is DELETE_OLD where older records are deleted to make space for new ones. 

	To accomodate this policy, now the producer has to pop() old records then push() new records to the ring buffer. 

	Invariant Violation: This breaks the core invariant of our current SPSC based design where producer is allowed to only modify buffer tail(push operation), and consumer allowed to modify head (pop operation). 
	
	Limition: With current design it is not possible to maintain ring correctness if producer and consumer both modify head without synchronization even for SPSC. We can implement this once synchronization is implement for ring buffer access 
