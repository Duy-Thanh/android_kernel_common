/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Standard definitions and types for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_STD_H
#define _NOLIBC_STD_H

/* Declare a few quite common macros and types that usually are in stdlib.h,
 * stdint.h, ctype.h, unistd.h and a few other common locations. Please place
 * integer type definitions and generic macros here, but avoid OS-specific and
 * syscall-specific stuff, as this file is expected to be included very early.
 */

/* note: may already be defined */
#ifndef NULL
#define NULL ((void *)0)
#endif

#include "stdint.h"

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#include <linux/types.h>

/* those are commonly provided by sys/types.h */
typedef unsigned int          dev_t;
typedef unsigned long         ino_t;
typedef unsigned int         mode_t;
typedef   signed int          pid_t;
typedef unsigned int          uid_t;
typedef unsigned int          gid_t;
typedef unsigned long       nlink_t;
typedef   signed long         off_t;
typedef   signed long     blksize_t;
typedef   signed long      blkcnt_t;
typedef __kernel_old_time_t  time_t;
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
/* those are commonly provided by sys/types.h */
typedef unsigned int          dev_t;
typedef unsigned long         ino_t;
typedef unsigned int         mode_t;
typedef   signed int          pid_t;
typedef unsigned int          uid_t;
typedef unsigned int          gid_t;
typedef unsigned long       nlink_t;
typedef   signed long         off_t;
typedef   signed long     blksize_t;
typedef   signed long      blkcnt_t;
typedef   signed long        time_t;
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

#endif /* _NOLIBC_STD_H */
