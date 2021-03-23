#ifdef CONFIG_ARM64_MODULE_PLTS
SECTIONS {
	.plt 0 (NOLOAD) : { BYTE(0) }
	.init.plt 0 (NOLOAD) : { BYTE(0) }
	.text.ftrace_trampoline 0 (NOLOAD) : { BYTE(0) }

#ifdef CONFIG_CRYPTO_FIPS140_INTEGRITY_CHECK
#define INIT_CALLS_LEVEL(level)						\
		KEEP(*(.initcall##level##.init*))			\
		KEEP(*(.initcall##level##s.init*))

	.initcalls : {
		*(.initcalls._start)
		INIT_CALLS_LEVEL(0)
		INIT_CALLS_LEVEL(1)
		INIT_CALLS_LEVEL(2)
		INIT_CALLS_LEVEL(3)
		INIT_CALLS_LEVEL(4)
		INIT_CALLS_LEVEL(5)
		INIT_CALLS_LEVEL(rootfs)
		INIT_CALLS_LEVEL(6)
		INIT_CALLS_LEVEL(7)
		*(.initcalls._end)
	}
#endif
}
#endif
