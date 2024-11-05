
ARCH=arm64
CROSS_COMPILE=aarch64-linux-gnu-

obj-m += tpu_hw_driver.o

# Assumes that the Linux Kernel source code is in:
KERNEL_DIR ?= $(HOME)/linux-stable

# Compile the module.
all:
	make -C $(KERNEL_DIR) \
		ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(PWD)

clean:
	make -C $(KERNEL_DIR) \
		ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
		M=$(PWD) clean

deployqemu:
	scp -P 8022 ./*.ko root@127.0.0.1:~
