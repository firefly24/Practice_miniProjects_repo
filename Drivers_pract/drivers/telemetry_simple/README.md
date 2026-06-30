Build a reliable and reusable simple telemetry data transport module that is based on simple producer-consumer design, which can be reused for sensors, stats collection, camera frames, DMA pipelines etc to deliver telemetry to userspace.


##Core Architecture:

Producer -> Telemetry module -> Buffering -> Synchronization -> Userspace consumer

##Submodules and their responsibilities
---
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
