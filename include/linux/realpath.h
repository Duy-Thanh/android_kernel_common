/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_REALPATH_H
#define _LINUX_REALPATH_H

char *tomoyo_get_absolute_path(const struct path *path, char * const buffer,
                                      const int buflen);

#define PR_INFO_FILE(f) \
	do { \
		char *buf = NULL; \
		char *str = NULL; \
		unsigned int buf_len = PAGE_SIZE / 2; \
		buf = kmalloc(buf_len, GFP_NOFS); \
		if (buf) { \
			str = tomoyo_get_absolute_path(&(f->f_path), buf, buf_len - 1); \
			pr_info("file:%s", str); \
			kfree(buf); \
		} \
	} while(false); \

#endif /* _LINUX_REALPATH_H */
