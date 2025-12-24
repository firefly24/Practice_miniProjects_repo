
The Kernel context is a set of rules that the kernel is operating under.

Context =  execution environment + scheduling rules + locking rules + sleeping rules

#### A task can be scheduled by:
	- Process scheduler
	- IRQ hardware
	- internal kernel mechanisms

### Process Context 
	- Running code on behalf of a process
	- This context can be entered by syscalls, file operations, sysfs, ioctl etc 
	- Can sleep and block
	- Can allocate kernel memory
	- Can use mutexes and locks
	- Can access userspace
	- Can be preemppted by other tasks

	Examples of calls that can run in this context: 
		- sysfs read/writes
		- file open/read/write
		- module probe() and remove()
		- workqueue handlers

### Atomic context
	- This is a restrictive context, which sets a rule that kernel must not sleep when running in this context.
	- Since no sleep and scheduling is allowed, the operations give the appeearance of being "atomic"
	- Cannot sleep or block or scheduled
	- Cannot use mutexes

	Examples:
		- holding a spinlock
		- when IRQ disabled (no preemption case)
		- while executing hard IRQ handler
		- while executing softIRQ handler
		- timer callback

### IRQ top half
	- This context is the hardware interrupt handler, which runs immediately when IRQ fires.
	- The hard IRQ handler must do the absolute minimum and return as soon as possible, and not spend much time in the handler. Example, just capture the new state of the device and exit.
	- So ideally it should acknowledge interrupt -> capture the state -> get out and defer the actual work of handling state change to another context
	- Cannot sleep, block or use mutexes
	- Cannot allocate mem, except GFP_ATOMIC
	- very time critical


### SoftIRQ
	- This is an atomic-ish context to run deferred interrupt work, runs in IRQ context but after hard IRQ
	- Cannot sleep , block, or use mutexes
	- Can run on any CPU
	- Can run repeatedly
	- Can be preemted by hard IRQ 
	- softIRQs can be run in process context as well when scheduled on ksoftirqd kernel thread, but atomic rules still apply


### Timer callback
	- code run when a kernel timer expires
	- Cannot sleep, block , scehdule or use mutexes
	- Run in softIRQ context


### IRQ thread
	- This runs kernel created thread for IRQ handling using api request_threaded_irq()
	- This is a process context but is owned by the IRQ subsystem
	- Can sleep, block and use mutexes
	- Must exit reasonably fast to avoid irq backlogs
	- if sleep or mutex is required in IRQ handling, we can use this

### Workqueue
	- This is a kernel worker thread, which handled the deferred work (deferred work context). 
	- This is not atomic in nature
	- Can sleep, block, and use mutexes
	- Can allocate mem
	- In this context, the work will be scheduled explicity



|Context|Can sleep?|Can allocate (GFP_KERNEL)?|Can mutex?|
|---|---|---|---|
|Process context|✅|✅|✅|
|Workqueue|✅|✅|✅|
|Threaded IRQ|⚠️ (limited)|⚠️|⚠️|
|Hard IRQ|❌|❌|❌|
|Softirq|❌|❌|❌|
|Timer|❌|❌|❌|
