# Character device with blocking I/O
\< *Document as per pre-code-work.md*  \>

How does a kernel module puts a userspace process to sleep until some specific condition occurs without busy-waiting ? 
The kernel has 3 options, till the condition is right :
1. ~~busy wait till condition becomes right~~
2. Return an error
3. Put process to sleep till condition becomes right (**Blocking I/O**)

## Must read: 
- Process states and sleeping in kernel
- Why busy-waiting is forbidden in kernel code
- Conditions based waiting and wait queues
- Blocking vs. non-blocking I/O semantics
- Signals and interruptible sleep

### What does sleeping mean for a process
- Voluntarily give up CPU
- Transitioning into sleep state
- Trusting the kernel to eventually reschedule it later
- Sleeping does not mean progress will happen, it means wake it up when progress **might** have happened.

### What is a wait queue
	It is a list of sleeping processes that are waiting for the same condition to be true. If there are two independent conditions to be waited on for a process, there shall be a separate waitqueue for each of the conditions.
	It is not a thread, or timer, just a simple queue.
	
	- the wait on a condition, not on time
	- waits in condition, not on an event
	  
	  Why?
		- wake ups on a process can be spurious
		- Multiple other reasons in the kernel can wake a process
		- Whereas a condition can be checked at present to snapshot the true current reality of the process.
		  
	When is the sleeping process woken-up:
		- When the waiting condition state is changed by some other context
		- The wake-up from other context will wake all process waiting on the same condition.
		  
	Since the wake ups can be spurious due to other reasons as well, the waking process should check the condition again, and go back to sleep if condition is still false.
	- Wakeups are to be treated as hints, not guarantees. Conditions are the truth. So it is necessary to revalidate the condition after waking.

	🔹 **If waking one condition should not wake waiters for another, use separate wait queues.**  
	🔹 **If wakeups are “just a hint to re-check state”, one queue may be enough.**

---
### Blocking vs non-blocking I/O
	Need to cover extensively, concepts, best practices, decisiions
	
	 Blocking vs Non-Blocking is NOT an I/O detail
	It is a **control-ownership decision**
	
	The deepest insight is this:
	**Blocking vs non-blocking decides who controls time.**

### Blocking read
Blocking is **strong coupling** between process and kernel.
- **Kernel controls time**
- Kernel decides _when_ the process runs
- Process hands control to the kernel scheduler

This implies:
- kernel must be trusted
- kernel must be correct
- kernel must not deadlock
- kernel must wake _exactly when safe_

### Non-blocking read
Non-blocking is **weak coupling**.
- **Userspace controls time**
- Kernel only reports state
- Process decides retry strategy

This implies:
- userspace owns complexity
- kernel must be predictable, not clever
- kernel must never sleep unexpectedly

---
## Layer 1:  System problem:

	Why does this module exist ? 
		We want a kernel mechanism which allows a userpace process to: 
			Wait efficiently for an event without burning CPU or polling,
			While preserving scheduler fairness
			and without losing wakeups
		Example scenarios:
			waiting for input on a terminal
			waiting on data on a pipe
			waiting for readiness on a socket
			waiting for HW interrupt


## Layer 2: Mental Model

	Actors:
		- Reader process
		- Writer process
		- Kernel driver
		- Scheduler
		  
	Shared state:
		- A condition: "Is this condition satisfied for required event to occur"
		
	Ownership:
		Kernel Owns:
			- Sleep-wakeup mechanism
			- scheduler interaction
			- Teardown semantics
		Userspace owns:
			- when to read
			- when to write
			- Orchestration
	
	This design:
		- cannot lose wakeups
		- tolerates spurious wakeups
		- respects scheduler fairness
		- preserves liveness
		- respects userspace policy
		- survives teardown
		
	Concurrency handling:
		- condition(truth) should be lock protected
		- wakeups are hints, state is authority
		- liveness > micro-optimizations


## Layer 3: Invariants

These rules must always hold, if any of these is violated ,then the driver is incorrect.
#### Safety Invariants - must never be violated

	1. No busy waiting should happen
		- A blocking call must never spin
		- CPU utilized by this process while waiting should be zero
		- violating this rule my cause scheduler starvation
		
	2. No lost wakeups
		-If a condition for an event to occur becomes true after a process is waiting, the waiting process must eventually wake.
		- violating this rule might cause silent infinite sleep
		  
	3. Wake-ups are hints, not gurarantee of condition truths
		- A wakeup alone does not guarantee progress
		- The condition must be validated to true for progress
		- violating this rule might cause race conditions and false progress
		
	4. Sleeping process must not hold locks
		- No sleep while holding locks is required by the waking process to avoid deadlocks due to multiple dependencies.
		- violating this rule will cause deadlock

#### Liveness invariants - must eventually happen

	1. If the condition becomes true, progress must be possible
		- There should be never a state/situation where condition is true, but sleeper process never wakes (lost wakeup).
		- Whereas on the other hand, it is better to have spurious wakeups as conditions can be validated on wakeup, but lost wakeup is a hazard.
		- Violation of this rule will cause system hangs.
		  
	2. Sleeper processes must be unblocked on teardown
		- When the associated device/module is destroyed, sleeper must be awakened or forced to exit with an error
		- Violation will result in stranded tasks
		- Every blocking path must have a termination path
		- 

#### State invariants - state machine correctness

	1. Condition is the single source of truth
		- Event occurency is encoded in state
		- truth is not inferred from history
		- truth is not inferred from just wakeups
		- condtions make restarts and spurious wakeups safe


## Layer 4: Failure modes - how could our design break

	1. Lost wakeup
		Cause: 
			- waker sets condition before waiter process sleeps
			- Waiting process sleeps without rechecking condition
		Consequences:
			- Waiting process might sleep forever
		Mitigation:
			- Always check condition inside wait logic
	
	2. Spurious wakeup misuse
		Cause:
			- waiting process assumes wakeup == event occured
			- Doesn't revalidate condition
		Consequences:
			- waiter process proceeds without correct event
			- state gets corrupted
		Mitigation:
			- Condition based waiting only
	
	3. Race during teardown
		Cause:
			- Module exits while waiting process sleeps
			- No wakeup issued by waker process
			- Condition never toggles again
		Consequences:
			- Process hang
		Mitigation:
			- Explicit teardown signalling
			- wake all sleeper process during exit
	  
	4. Sleeping with locks held
		Cause:
			- Waiting processs sleeps while holding lock
			- waker process needs same path to signal wakeup
		Consequences:
			- Deadlock
		Mitigation:
			- Follow lock discipline before sleep


## Layer 5: Code

	- Must use wait queues
	- Must use condition based waiting
	- Must support interruptible sleep
	- Must have explicit teardown signalling

## Layer 6: DSA 

	1. Intrusive doubly linked list
	2. state machines
	3. producer-consumer co-ordination
	4. broadcast wakeups
	5. scheduler runqueue vs waitqueue
	
	
## Updates:

#### 04/02/2025:
	Implemented minimal blocking read and write on a char device.
 
	Thoughts:
	Since actual data transfer support is not added here, doesn't this module look analogous to synchronization primitives like condition_variables, semaphores, futex ? But this resides in kernel/device boundary rather than shared userspace memory.
 
	Can this be changed to behave as a barrier mechanism, by adding a global reader count per device file perhaps, and using a threshold count to issue wakeup_all by writer? Maybe this counting machanism can be added in open/close fileops calls rather than read/writes.  





	

