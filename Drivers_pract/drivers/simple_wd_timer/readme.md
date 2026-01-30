
Resources:
- https://elixir.bootlin.com/linux/v6.18/source/Documentation/watchdog/watchdog-api.rst
- https://elixir.bootlin.com/linux/v6.18/source/Documentation/watchdog/watchdog-kernel-api.rst


Overview of SW watchdog: 
	- This sw watchdog is a kernel module that keeps track of a user-space daemon/service, whether it is alive or not.
	- the watchdog keeps running in the background once armed...
	  \<continue explaining the watchdog module here - purpose, timer used, which kernel context, char device for userspace communication etc\>   


## Important concepts: 
 - "armed" semantics
 - "nowayout" semantics
 - sw watchdog vs hardware watchdog
 - platform devices vs dynamically created char devices
 - char device file operations
 - user-kernel communication


### Important points to note: 
- timer shutdown vs. timer delete for resusable timers
- teardown char device before timer is deleted to avoid ping/delete  race.
- s/w watchdog module unload when "nowayout" is set
- watchdogs make sense for userspace daemon processes only, not for process that have a natural lifecycle end
- watchdogs are more about liveness of a process than security
- Kernel assumes access control policy belongs to userspace


### Questions for future
- Current module is single instance only, how to modify for multi-instance
- Any other user process may hijack the watchdog, how to ensure exclusive access to /dev/sw_wdog to prevent this
- handling cases where user process may exit before arming watchdog
- magic close
- systemd integration

