// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2026 Google LLC.

#ifndef LINUX_HELPERS_H_
#define LINUX_HELPERS_H_

#include <linux/fs.h>

bool my_get_close_on_exec(int fd);

struct inode *my_file_inode(const struct file *f);
void my_i_size_write(struct inode* inode, loff_t i_size);
loff_t my_i_size_read(const struct inode *inode);

void my_i_mmap_lock_read(void* mapping);
void my_i_mmap_unlock_read(void* mapping);
bool my_rb_empty_root(const struct rb_root* root);

void my_init_sync_kiocb(struct kiocb *kiocb, struct file *filp);

unsigned long my_file_count(struct file* filp);

#endif
