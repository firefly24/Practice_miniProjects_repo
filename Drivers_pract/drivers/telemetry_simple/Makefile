obj-m := simple_telemetry_drv.o

simple_telemetry_drv-objs := src/telemetry_main.o src/telemetry_ring.o

kernel_header_path := /lib/modules/$(shell uname -r)/build


all:
	$(MAKE) -C $(kernel_header_path) M=$(PWD) modules
clean:
	$(MAKE) -C $(kernel_header_path) M=$(PWD) clean
