#include <linux/printk.h>
#include <linux/syscalls.h>
#include <linux/dma-map-ops.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/time64.h>

#include <asm/cacheflush.h>


void fonger_hello_world(void)
{
	printk("Fonger - Hello");
}






SYSCALL_DEFINE3(fonger_syscall, long, operation, long, test_size, long, dummy)
{
	long bufsize;
	int* ptr;
	long i;

	struct timespec64 before, after;



	printk("Fonger - Syscall");
	printk("Fonger arg: %ld, %ld, %ld", operation, test_size, dummy);

	bufsize = test_size / sizeof(int);
	ptr = kmalloc(sizeof(int)*bufsize, GFP_DMA);

	printk("\t Fonger Alloc buffer size = %ld,  @ %p", sizeof(int)*bufsize, ptr);


	printk("\t Fonger dummy write and read the element to make it cahced");
	for (i = 0; i < bufsize; i++) {
		ptr[i] = i;
	}

	for (i = 0; i < bufsize; i++) {
		if (ptr[i] == -1) {
			ptr[i] = ptr[i] + i;
		}
	}
	for (i = 0; i < bufsize; i++) {
		int* address;
		address = &ptr[i];
		if ((i % 1000) == 0) {
			printk("\t Fonger Alloc buffer @ %p = %d", address, ptr[i]);
		}
	}


	printk("Start");
	if (operation == 0) {
		printk("Fonger do flush");
		ktime_get_real_ts64(&before);
		arch_sync_dma_for_device(virt_to_phys(ptr), sizeof(int)*bufsize, DMA_TO_DEVICE);
		ktime_get_real_ts64(&after);
	}
	else {
		printk("Fonger do invlaid");
		ktime_get_real_ts64(&before);
		arch_sync_dma_for_cpu(virt_to_phys(ptr), sizeof(int)*bufsize, DMA_FROM_DEVICE);
		ktime_get_real_ts64(&after);
	}

	printk("END");

	printk("Time before: %ld sec, %ld ns\n", before.tv_sec, before.tv_nsec);
	printk("Time after: %ld sec, %ld ns\n", after.tv_sec, after.tv_nsec);

	kfree(ptr);


	return 999;

}










