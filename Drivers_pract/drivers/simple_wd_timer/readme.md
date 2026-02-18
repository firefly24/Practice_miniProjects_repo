
Resources:
- https://elixir.bootlin.com/linux/v6.18/source/Documentation/watchdog/watchdog-api.rst
- https://elixir.bootlin.com/linux/v6.18/source/Documentation/watchdog/watchdog-kernel-api.rst


## Layers of understanding:

### Layer 1: System problem

#### What is the system problem we're trying to solve?
	 When we have some userspace system daemons that need to be up and running all the time for our device health, security and functioning, and when these processes crash or become unresponsive, the device maybe left in a unsafe and undefined state. We need a robust mechanism to detect such failures and trigger a recovery action such as system reset.  
#### What failure does it prevent ? 
	- We need some mechanism which can keep checking the liveness of such processes and trigger a reset/recovery when any of these critcal process crashes to prevent our device being left in a vulnerable or degraded state. 
	- We call such mechanism a watchdog, which needs petting periodically from the userspace process daemon to notify of its liveness. IF that process does not pet the watchdog within the specified timeout interval, our watchdog must consider the process has become unresponsive or failed and trigger a system reboot/recovery. 

#### What mechanisms exist ?

A watchdog can be implemented 2 ways, either hardware based or sofrware based, each having its own advantages and limitations.
##### Hardware Watchdog:
	In HW based watchdog, there is a separate hardware component that takes care of pinging, timeouts, resets etc. They are usually more robust as in case there is a system wide hang, a SW watchdog will also freeze and will no longer be reliable, whereas a HW watchdog will be independent of such hangs and can issue a reboot immediately. A HW watchdog driver can use the existing kernel watchdog framework to create and drive a watchdog device. The framework standardizes device registration, control and userspace interface.
##### Software Watchdog:
	For a SW watchdog, no separate HW component is needed. We write our own software kernel module to implement a watchdog mechanism, where the SW kernel module keeps monitoring the userspace daemon. We use kernel timers framework to implement this, and keep updating the timer with every ping from userspace. If the timer is not updated before timeout, we trigger a reset in timer callback. Since the timer callback runs in softIRQ context, we have to make sure that our callback function must not have sleeping or blocking operations.
	The disadvantage here is if there is a system wide hang, or OOM issues, starvation, scheduler lockup or kernel panic, the SW watchdog is no longer reliable.
##### Combined approach:
	In some cases, a combination of SW and HW watchdog is used. Primarily when SW watchdog stops responding, the HW watchdog saves the day by triggering reset.

---

## <TODO>

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

