/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/unistd.h>

	.text
	.global _start
	.type _start, %function
_start:
	mov	x0, xzr
	mov	x8, #__NR_brk
	svc	#0

	str	x0, [sp, #-16]!
	mov	x0, #3
	mov	x1, sp
	mov	x2, #8
	mov	x8, #__NR_write
	svc	#0

	mov	x0, xzr
	mov	x8, #__NR_exit
	svc	#0
	.size _start, .-_start
