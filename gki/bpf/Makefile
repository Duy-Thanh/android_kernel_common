# SPDX-License-Identifier: GPL-2.0-only

obj-m += bpf_fd_loader.o

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean

