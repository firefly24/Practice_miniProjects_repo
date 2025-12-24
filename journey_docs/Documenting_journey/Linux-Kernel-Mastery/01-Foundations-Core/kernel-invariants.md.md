
3 Important questions to ask:
	 1. What context am I in ?
	 2. Which invariants apply here?
	 3. Who owns scheduling right now - me or kernel ?

#### Invariant 1: Must not sleep in atomic context
	Sleeping is illegal if any of the following cases hold true:
		- Interrupts are disabled
		- Preemption is disabled
		- in softirq/ hardirq / timer context
		- holding a spinlock
		
	Example where sleeping is involved (therefore must be avoided)
		- msleep()
		- mutex_lock()
		- wait_event()
		- schedule()
		- mem alloction using GFP_KERNEL (as the *alloc fn can sleep)

	Consequences of violating this invariant: 
		- hard lock-ups
		- warnings
		- ramdom crashes


#### Invariant 2: Hard IRQ handlers must be short, fast and bounded
	- Interrupts are disabled globally and locally during this state
	- So the handler must not sleep, block, or perform unbounded work and must finish quickly as time latency is very important here


#### Invariant 3 : Do not try to trigger hard IRQ through some software simulation path using random  interrupt APIs
	- Using apis such as generic_handle_irq() to simulate a HW IRQ trigger is a bad idea, as it is not a valid kernel interrupt path.
	- When this api was used, I saw that disabling IRQ before calling this api and re-enabling IRQs had to be done manually to obey the rules of IRQ context.


#### Invariant 4: Every device instance should have its own private state.
	- Multiple physical devices can use the same driver to drive themselves, so each device instance should hold its own private data structures to keep track of its own state.
	- So such variables tracking the state of a device should not be global in nature in the driver code
	- constants, and shared data can be global 

### IRQ related 
	- Never trigger IRQs manually from drivers
	- Never assume IRQ context
	- IRQ core owns interupt state

### Concurrency
	- Never store device state in globals for multi instance devices
	- Once device -> one private struct of driver_data
	- Anything shared must be protected

### Sysfs
	- sysfs callbacks run in process context
	- must be fast and safe
	- Must not call core kernel machinery

### devm
	- devm frees only on device removal
	- devm does not protect against races
-

| Context         | Can sleep?   | Can allocate (GFP_KERNEL)? | Can mutex? |
| --------------- | ------------ | -------------------------- | ---------- |
| Process context | ✅            | ✅                          | ✅          |
| Workqueue       | ✅            | ✅                          | ✅          |
| Threaded IRQ    | ⚠️ (limited) | ⚠️                         | ⚠️         |
| Hard IRQ        | ❌            | ❌                          | ❌          |
| Softirq         | ❌            | ❌                          | ❌          |
| Timer           | ❌            | ❌                          | ❌          |

