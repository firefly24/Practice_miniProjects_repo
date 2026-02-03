obj-m := chardev_blocking.o

chardev_blocking-objs := src/chrdev_blk_io.o

#QEMU kernel build source code dir
qemu_kdir := $(HOME)/EmbAI/sys_mastery/qmuk_kernel/linux

all:
	$(MAKE) -C $(qemu_kdir) M=$(PWD) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules

clean:
	$(MAKE) -C $(qemu_kdir) M=$(PWD) clean
