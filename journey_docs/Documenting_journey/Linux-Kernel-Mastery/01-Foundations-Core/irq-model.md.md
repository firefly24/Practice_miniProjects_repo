
## Linux IRQ Model – Practical Notes

### IRQ ownership
- IRQs are owned by the kernel IRQ core
- Drivers only register handlers

### IRQ contexts
- Hard IRQ: no sleeping, minimal work
- Threaded IRQ: may sleep, real logic
- Process context: sysfs, ioctl, user input

### Invariants learned
- Never call generic_handle_irq() from drivers
- Never enable IRQs inside an IRQ handler
- Never sleep in hard IRQ context

### Debug lesson
If kernel warns about IRQ state:
- You violated an IRQ invariant
- Fix the *context*, not the warning


### Only IRQ core is allowed to drive this pipeline

CPU exception entry
   ↓
irq_enter()
   ↓
handle_irq_desc()
   ↓
handle_irq_event()
   ↓
┌─────────────────────────────┐
	   hard IRQ handler (top half)                                                                   
└─────────────────────────────┘
   ↓
(if IRQ_WAKE_THREAD)
   ↓
┌─────────────────────────────┐
	 threaded IRQ handler  
└─────────────────────────────┘
   ↓
irq_exit()


### Hard IRQ handler signature
	
	static irqreturn_t my_hardirq_handler_func(...)
	{
		// Capture state here, keep as short as possible
		
		 return IRQ_WAKE_THREAD;  // if you have threaded irq handler
		//or else
		return IRQ_HANDLED; // Signal hw ird handled
	}


### Threaded IRQ handler signature (bottom half)

	static irqreturn_T my_threadedIRQ_handler_func(...)
	{
		// Do the work based on captured state
			return IRQ_HANDLED;
	}


### How to bind hardIRQ handler and threaded handler together

	int ret = devm_request_threaded_irq(my_dev,irq_num,
					my_hardirq_handler_func,	// top half 
					my_threadedIRQ_handler_func,  //bottom half
					 IRQF_ONESHOT| IRQF_TRIGGER_RISING, //flags
					"dev_name", pdev);
					
	OR use unmanaged version:
			request_threaded_irq(...)
## Important rules to remember

| Mechanism    | Who uses it                                 |
| ------------ | ------------------------------------------- |
| Hard IRQ     | Hardware entry                              |
| Softirq      | Core kernel subsystems (NET, block, timers) |
| Tasklet      | Legacy softirq wrapper                      |
| Threaded IRQ | Device drivers                              |
| Workqueue    | Deferred, sleepable work                    |

### Why device driver devs  should almost never use softirqs directly
- softirqs are per-CPU
- non-sleepable
- hard to reason about
- easy to starve the system



GPIO pin toggles 
        ↓
GPIO controller (PL061) detects edge
        ↓
GPIO controller asserts interrupt line
        ↓
Interrupt controller (GIC) raises IRQ N
        ↓
CPU jumps into exception vector


### Resources:
- Understanding Linux Interrupt subsystem by Priya Dixit : https://www.youtube.com/watch?v=LOCsN3V1ECE&t=1400s
- Udemy Course- Interrupts and Bottom Halves in Linux Kernel : https://www.udemy.com/share/103AoS3@7wVM6FEZfU8H6ixUQg2T85ISg-5WUg7B79XbLSV6JJE_-PjokR4ImHILgrpFdGCs/
- Talks on Linux internals by Prof. Smruti R. Sarangi https://youtu.be/5gr8R1s0oys?si=rVw07PKITG8AbPLV
- Workqueues in Linux : https://youtu.be/Ht6AK_6dTRE?si=4Vvgzn1LFDFe_fhI