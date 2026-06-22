Build a reliable and reusable simple telemetry data transport module that is based on simple producer-consumer design, which can be reused for sensors, stats collection, camera frames, DMA pipelines etc to deliver telemetry to userspace.


Core Architecture:

Producer -> Telemetry module -> Buffering -> Synchronization -> Userspace consumer
