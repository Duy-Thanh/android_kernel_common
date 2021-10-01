
#include <linux/linkage.h>
#include <asm/assembler.h>

	.section	".init.rodata", "a"

	.align	2
	.globl	fips140_rela_text
fips140_rela_text:
	.weak	.rela.text
	.long	.rela.text - .
	.long	0

	.globl	fips140_rela_rodata
fips140_rela_rodata:
	.weak	.rela.rodata
	.long	.rela.rodata - .
	.long	0
