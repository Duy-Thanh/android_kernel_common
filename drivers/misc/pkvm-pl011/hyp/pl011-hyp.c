#include <asm/alternative-macros.h>
#include <asm/barrier.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_pkvm_module.h>
#include <asm/io.h>

// TODO: Kconfig!
#define HYP_PL011_BASE_PHYS	0x09000000
#define HYP_PL011_UARTFR	0x18
#define HYP_PL011_UARTFR_BUSY	3
#define HYP_PL011_UARTFR_FULL	5

static unsigned long uart_addr;

static inline unsigned int __hyp_readw(void *ioaddr)
{
	unsigned int val;
	asm volatile("ldr %w0, [%1]" : "=r" (val) : "r" (ioaddr));
	return val;
}
static inline void __hyp_writew(unsigned int val, void *ioaddr)
{
	asm volatile("str %w0, [%1]" : : "r" (val), "r" (ioaddr));
}

void pl011_hyp_putc(char c)
{
	unsigned int val;
	void *base = (void *)uart_addr;

	do {
		val = __hyp_readw(base + HYP_PL011_UARTFR);
	} while (val & (1U << HYP_PL011_UARTFR_FULL));
	__hyp_writew(c, base);
	do {
		val = __hyp_readw(base + HYP_PL011_UARTFR);
	} while (val & (1U << HYP_PL011_UARTFR_BUSY));
}

struct putc_fn_struct {
	void (*putc_fn)(char c);
};

const struct putc_fn_struct putc_struct = { .putc_fn = pl011_hyp_putc };

static void pl011_hyp_puts(const char *str)
{
	while (*str)
		pl011_hyp_putc(*str++);
	pl011_hyp_putc('\n');
}

int pl011_hyp_init(const struct pkvm_module_ops *ops)
{
	int ret;

	ret = ops->create_private_mapping(HYP_PL011_BASE_PHYS, PAGE_SIZE,
					  PAGE_HYP_DEVICE, &uart_addr);
	if (ret)
		return ret;

	ret = ops->register_serial_driver(putc_struct.putc_fn);
	if (ret)
		return ret;

	pl011_hyp_puts("Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aliquam euismod placerat mauris sit amet faucibus. Ut at arcu fringilla, accumsan sem sit amet, fermentum lorem. Quisque nisi leo, auctor quis leo vitae, aliquam ultricies justo. Mauris ac ultricies tellus, a dignissim nulla. Cras facilisis dolor ut purus cursus pellentesque. Nam ac scelerisque mauris, in bibendum ante. Vivamus mollis, elit eu vestibulum aliquam, orci nunc feugiat mi, a bibendum felis tellus ac purus. Nunc felis sapien, lacinia sit amet mauris nec, placerat volutpat nibh. Nam et nibh massa. Maecenas vel volutpat metus. Nunc neque augue, rutrum in felis et, posuere rutrum eros.");

	return 0;
}
