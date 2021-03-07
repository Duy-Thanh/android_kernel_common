SECTIONS {
<<<<<<< HEAD   (d75055 BACKPORT: drm/virtio: use kvmalloc for large allocations)
	.plt : { BYTE(0) }
	.init.plt : { BYTE(0) }
	.text.ftrace_trampoline : { BYTE(0) }
=======
	.plt 0 (NOLOAD) : { BYTE(0) }
	.init.plt 0 (NOLOAD) : { BYTE(0) }
	.text.ftrace_trampoline 0 (NOLOAD) : { BYTE(0) }
>>>>>>> BRANCH (1d177c Linux 4.14.224)
}
