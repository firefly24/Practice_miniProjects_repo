

### **GPIO Subsystem Documentation Sources:**

1. `Documentation/driver-api/gpio/consumer.rst`
2. `drivers/gpio/` (real code examples)
3. `include/linux/gpio/consumer.h` (actual API definitions)
4. Device Tree bindings: `Documentation/devicetree/bindings/gpio/`
5. LWN articles on GPIO descriptors
6. Greg KH talks
7. drivers/leds/leds-gpio.c

Referring this course to understand GPIO system from ground up : 
https://www.udemy.com/course/mastering-microcontroller-with-peripheral-driver-development/learn/lecture/14265502#overview

### Important concepts: 

1. GPIO port and pin map and registers
2. GPIO modes- input/output
3. Gpio config - push-pull/ pull-up/ pull-down/ open-drain
4. Enabling GPIO peripheral clocks before accessing gpio registers
5. GPIO alternate functions
6. GPIO interrupts and Edge detection
7. Debouncing
8. Input/output latching

GPIO Subsystem architecture - consumer provider model




