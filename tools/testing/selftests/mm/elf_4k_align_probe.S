/* SPDX-License-Identifier: GPL-2.0 */

	.section .text
	.global _start
_start:
	adrp	x9, bss_probe
	add	x9, x9, :lo12:bss_probe
	mov	w10, #0x5a
	strb	w10, [x9]
	ldrb	w11, [x9]
	cmp	w10, w11
	b.ne	.Lfail

	/* The first 4K page past the BSS must still be unmapped. */
	adrp	x0, bss_probe_end
	add	x0, x0, :lo12:bss_probe_end
	add	x0, x0, #0xfff
	and	x0, x0, #~0xfff
	mov	x9, x0
	mov	x1, #4096
	mov	x2, #3
	mov	x3, #0x22
	movk	x3, #0x10, lsl #16
	mov	x4, #-1
	mov	x5, #0
	mov	x8, #222
	svc	#0
	cmp	x0, x9
	b.ne	.Lfail
	mov	x1, #4096
	mov	x8, #215
	svc	#0
	mov	x0, #0
	b	.Lexit

.Lfail:
	mov	x0, #1
.Lexit:
	mov	x8, #93
	svc	#0

	.section .bss
	.balign 8
bss_probe:
	.skip	8192
bss_probe_end:
