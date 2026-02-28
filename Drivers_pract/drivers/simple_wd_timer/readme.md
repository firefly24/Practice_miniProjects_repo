
Resources:
- https://elixir.bootlin.com/linux/v6.18/source/Documentation/watchdog/watchdog-api.rst
- https://elixir.bootlin.com/linux/v6.18/source/Documentation/watchdog/watchdog-kernel-api.rst


## Layers of understanding:

### Layer 1: System problem

#### What is the system problem we're trying to solve?
- When we have some userspace system daemons that need to be up and running all the time for our device health, security and functioning, and when these processes crash or become unresponsive, the device maybe left in a unsafe and undefined state. 
- We need a robust mechanism to detect such failures and trigger a recovery action such as system reset.  
#### What failure does it prevent ? 
- We need some mechanism which can keep checking the liveness of such processes and trigger a reset/recovery when any of these critcal process crashes to prevent our device being left in a vulnerable or degraded state. 
- We call such mechanism a watchdog, which needs petting periodically from the userspace process daemon to notify of its liveness. IF that process does not pet the watchdog within the specified timeout interval, our watchdog must consider the process has become unresponsive or failed and trigger a system reboot/recovery. 

#### What mechanisms exist ?

A watchdog can be implemented 2 ways, either hardware based or sofrware based, each having its own advantages and limitations.
##### Hardware Watchdog:
- In HW based watchdog, there is a separate hardware component that takes care of pinging, timeouts, resets etc. 
- They are usually more robust as in case there is a system wide hang, a SW watchdog will also freeze and will no longer be reliable, whereas a HW watchdog will be independent of such hangs and can issue a reboot immediately. 
- A HW watchdog driver can use the existing kernel watchdog framework to create and drive a watchdog device. The framework standardizes device registration, control and userspace interface.
##### Software Watchdog:
- For a SW watchdog, no separate HW component is needed. We write our own software kernel module to implement a watchdog mechanism, where the SW kernel module keeps monitoring the userspace daemon. 
- We use kernel timers framework to implement this, and keep updating the timer with every ping from userspace. If the timer is not updated before timeout, we trigger a reset in timer callback. 
- Since the timer callback runs in softIRQ context, we have to make sure that our callback function must not have sleeping or blocking operations.
- The limitation here is if there is a system wide hang, or OOM issues, starvation, scheduler lockup or kernel panic, the SW watchdog is no longer reliable.
##### Combined approach:
- In some cases, a combination of SW and HW watchdog is used. Primarily when SW watchdog stops responding, the HW watchdog saves the day by triggering reset.

## Layer 2: Semantics and Invariants

### What does "armed" mean

- When a watchdog is "armed", it means the watchdog contract is active where the system has delegated the responsibility to our watchdog for monitoring the liveness of a critical userspace process, and trigger recovery if the contract is violated.
  
- When our watchdog module is loaded into the kernel, it does not imply the watchdog is armed. We leave this responsibility to userspace processes such as systemd to arm the watchdog when it enables monitoring.
  
- Once our watchdog is armed, it enforces a timeout window within which the observed daemon must "ping" the watchdog periodically to indicate forward progress. Each ping refreshes the timeout interval.
  
- If our watchdog is armed, and timeout expires without a ping due process crash or unresponsiveness, the system must not be left operational post timeout expiry without triggering the configured recovery mechanism.
  
- An armed watchdog must hold the following invariant: 
  The system must not continue normal operations post watchdog expiry without triggering recovery.


### What is the "pet" contract
- When armed defines watchdog responsibility is active, a "pet" acts as proof of forward progress and serves as a renewal of system's trust in the process.
- When watchdog is armed, the observed process must notify the watchdog periodically of its liveness , which is does by pinging the watchdog. This ping by the userspace process is called a "pet".
- A successful "pet" not only indicates proof of meaningful progress, it also refreshes the watchdog timeout window waiting for the next pet.
- Incorrect petting - petting must be done only by out legitimate observed process. Unauthorised or accidental petting by any unrelated processes can create a false sense of liveness, potentially allowing the system to remain operational while a critical system process may have crashed.
- The following invariant must hold: each pet guarantees the system may safely continue operation untill the next timeout window expires.


### Expiry semantics
-  When our watchdog expires, we mean that the system can no longer trust the observed process, the trust contract is broken here. The watchdog contract has been violated due to the absence of a valid pet within the expected timeout window.
- On watchdog expiry, the observed process no longer guarantees any meaningful forward progress to the system, and can cause the system to go in an unsafe or undefined state if normal operation continues.
- Invariant violated on expiry -  The system can no longer assume the observed process is healthy or making progress. In such case, the system must trigger an appropriate recovery action (panic, reset or reboot) to restore to a reliable state.
- Expiry must be deterministic in nature, so that any violation of the watchdog contract will have a safe and predictable recovery response.
- Why we treat expiry as failure truth - because the absence of forward progress within the timeout window for critical processes must be treated as sufficient evidence that process is no longer healthy. So expiry is not just a timeout, it is a decision boundary where we proactively act to avoid potential catastrophic issues.
- Expiry is the essential component of the watchdog contract, as without expiry  the watchdog is just a monitoring mechanism. Expiry converts the watchdog from monitoring to protection mechanism. Without expiry triggering a recovery, a watchdog will only observe failures but not prevent the system from continuing operating in unsafe state.

### Watchdog lifecycle overview
-  The watchdog operates primarily in two states - disarmed and armed. While armed, the watchdog relies in periodic pet events for renewing the timer timeout window. If no pet is received within the timeout window, the watchdog transitions to expired state.
- Watchdog is in the disarmed state when the module is loaded.  It is userspace responsibility to explicitly arm the watchdog when monitoring is required. While armed, the observed process keeps petting the watchdog periodically to renew timeout window, if it misses to pet within timeout window, the watchdog expires.
- Transition from disarmed to armed is initiated by userspace. In armed state it is observed process responsibility to keep petting to refresh monitoring window. Absence of pet within timeout window will trigger transition to expird state 
- Safety loop:  disarmed -> armed -> (pet events renewing monitoring window) -> expired -> recovery

### nowayout semantics
-  For some safety critical systems it might be a requirement that the **once the watchdog is armed it cannot be stopped/disarmed**. This mechanism is to prevent accidental disabling or malicious disabling of watchdog by any external entity. 
- So once watchdog is armed, it is committed to system safety without any backdoor way to back out of or revoke the contract- this is what we call "nowayout".
- For some safety critical processes, if some external entity manages to disable the watchdog while the critical process is running, the watchdog contract is no longer in effect, and should the critical process fail or hang in such scenario, we do not have a detecting such failure and recover system.
- While nowayout strengthens safety guarantees, it may be undesirable in development or testing environments where controlled shutdown or disabling of the watchdog is required.

### Ownership model
- The ownership model defines who is allowed to prove liveness to the watchdog, and therefore authorized to uphold the watchdog contract.
- Once armed, **a single legitimate authority must be allowed to issue pet signals to our watchdog** to maintain system integrity. Enforcing petting to only the observed process ensures liveness signals remain trustworthy.
- If any external or unrelated entity is able to hijack the watchdog petting, it might give a false sense of liveness  or mask failures of our monitored process,  which break the watchdog contract.
- Such a unauthorised petting can be used maliciously to compromise system correctness by preventing recovery actions from being triggered.
- The ownership model ensures that the pet signals remain meaningful by validating their source. Pet events from any unauthorised source are considered invalid and must not influence the watchdog state.

#### Ownership implementation strategies
How can the kernel ensure only one authority can pet the watchdog ?
This variant must be enforced - only one owner can hold authority at a time.

##### Exclusive open
- Only one process can open /dev/watchdog device . If another process tries to call open() after this, it must be rejected.
- This prevents multiple petters, accidental petting and hijacking
- Exclusive open must happen safely under concurrency. If two processes call open() at the same time, only one must succeed.  We can use atomic CAS operation to set a ownership flag when first open is called, preventing races between concurrent open() calls.
- Exclusive open is simple and robust logic ownership mechanism by establishing a clear authority boundary at device open.
###### Lifecycle of Exclusive open()
File operations: 
open() -> acquire ownership
write() -> validate owner before pet
close() -> drop ownership
Ownership is tied to the lifetime of the file descriptor. When the fd is closed, the kernel invokes release() file operation, allowing the ownership to be safely dropped. Failure to release ownership will be render the watchdog unusable in future as it is still holding stale ownership.

Special scenarios to consider:
- **Fork** — Since the child process inherits the file descriptor, it is treated as part of the same ownership context and may issue valid pet operations.
- **Crash** — If the owning process terminates unexpectedly, the kernel automatically closes the file descriptor and invokes `release()`, ensuring ownership is released.
- **Multiple threads** — Threads within the same process share the file descriptor and can safely issue pet operations as part of the same ownership domain.
- 
##### FD based ownership
- Authority is tied to file descriptor lifetime
- TODO

##### PID based ownership
- Track and validate owner process id
- TODO


#### Ownership touch points
Ownership should not be treated as a single event, it is a system invariant that must remain consistent across all execution paths that can observe or modify watchdog state.

API entry points that can interact with ownership state, so must validate or respect ownership rules:
- **open()**
  Responsible for acquiring ownership. It must enforce exclusive access using atomic CAS operation to set an ownership flag to prevent concurrent owners.
- **write()**
  Must validate whether the caller holds valid ownership before accepting a pet, preventing stray or malicious pets.
- **close() / release()**
  Responsible for releasing ownership, so that watchdog is not left in a stale locked state.
- **timer_callback()**
  Must respect ownership and armed state while deciding expiry action.
- **module exit()**
  Must ensure no active ownership remains or enforce shutdown semantics before teardown to prevent undefined behavior.
- interrupt or concurrent contexts must not bypass ownership validation.

#### Ownership  invariants
- Only one owner exists at a time
- Only owner may pet
- Ownership released on close
- Ownership cannot change while pet is processed
- Expiry must not accept pets after violation


#### Shared states:

1. Ownership
2. Armed
3. Timer

Ownership: 
	Modified in:
		open()
		release()
	Validated in:
		write()
		timer_callback()
	Therefore, ownership reads/modification must be done atomically or under the same synchronization lock in all these 4 entry points.

Armed state:
	Modified in:
		 arm
		 disarm
		 release
		 module exit
	Validated in:
		write
		timer_callback
    Therefore requires consistent synchronization.

Ownership count
	Ownership_count <= 1 should be maintained for exclusive ownership.
	ownership_count must be atomic and be modified by atomic CAS for concurrency safety.

## <TODO>

## Important concepts: 
 - ~~"armed" semantics~~
 - ~~"nowayout" semantics~~
 - ~~sw watchdog vs hardware watchdog~~
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

