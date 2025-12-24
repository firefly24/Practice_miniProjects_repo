
## Learnings while writing a basic module for led toggle using gpios

I was trying to build a LED toggle kernel module that uses GPIOs on QEMU to understand the basics of linux device driver development. Documenting my learnings below:

### Important data structures learnt: 
 - struct platform_device
 - struct device
 - struct device_driver
 - struct platform_driver

**struct device:**

	The fundamental core object representing a hardware device in a linux kernel device model.

**struct device_driver**

	This object represents the driver that can driver the struct device. So device_driver is the code written to drive the device, whereas struct device/platform_device is only used for describing and declaring the device to kernel.


**struct platform_device** 

	 This object encapsulates/wraps the struct device object, with additional platform specific info. To be more precise, platform_device represent and describe those device that are not auto discoverable like USB devices. The platform_device is a device that  needs to be described to our kernel in advance, as part of the device tree, for the kernel to drive them.
	
	By platform specifc info - it does not mean arch specific or vendor specific. It represents board specific info. Like what are the SoC resources this device is expected to use- like gpios, clocks, interrupt lines etc. These resources need to be declared well in advance for the device to work - with APCI/ DTSI
	
	 All the Platform_device reside on the platform_bus. Platform bus is the bus that holds all devices that are not discoverable by hardware enumeration. 
	 So the these on platform bus comes from : 
		 Device tree
		 ACPI
	And not auto-discoverable on hotplug lile 
		PCI devices
		USB devices
	Examples of platform_device :
		GPIO controllers
		LEDs
		Clocks
		Regulators
		Timers
		Pin controllers
	
	The platform bus is responsible for matching the platform_Device described in Device Tree, to the matching device_driver that can driver this matched device. Once it finds the compatible driver, it is responsible for calling the probe() on this device.
	 
		Device Tree
		   ↓
		OF core parses DT
		   ↓
		platform_device created
		   ↓
		struct device initialized
		   ↓
		platform bus matches driver
		   ↓
		probe() runs
		   ↓
		sysfs entries appear


When multiple hardware devices exist of same type, example - multiple gpios to drive multiple leds for different functions: 

In this kind of multi-instance scenario, the device_driver code that drive multiple led-gpios is same. What we can say is the device_driver drives multiple instances of struct device.

That is why, each of the device/platform_device objects should have their own private data store (struct driver_data) that stores the states and configurations related to that instance.
For example, let's say , we have 3 gpios to driver a red, green and blue led respectively, and we use a led_on variable to set gpio high, if the user process sets led_on , all the leds will be turned on as this will represent the global state of the driver. 
So each led gpios device instance should have its own copy of led_on variable, to control these leds individually. struct driver_data is the object we use to store device instance specific data.

struct platform_driver
	
	A platform_driver is not tied to a single device.
	It represents a *type of device*.

	One platform_driver can bind to:
	- one platform_device
	- or many platform_device instances

	Each successful match creates:
	- one probe() call
	- one independent device instance
	- one independent driver_data instance

	This is why drivers must never store device state in global variables.


Additional topics left to document - 
sysfs and kobj
OF core
devm* apis
resources

Other implementation specific learnings to document : 
sysfs vs. procfs
identifying illegal apis for driver development
identifying kernel invariants
IRQ model

Resources :
Udemy courses by Linux trainer: https://www.udemy.com/user/linux-trainer/
OS lectures by Prof. Smruti R Sarangi: https://youtu.be/Wdnwr_CDqrc?si=B56mxxqOfM9nNU9S
Linux Internals (Sysfs, proc, Udev): https://www.youtube.com/watch?v=H5QbulRfXTE
source code: sysfs device attributes:https://elixir.bootlin.com/linux/v6.15/source/include/linux/device.h
Embedded Systems : https://courses.pyjamabrah.com/web/courses