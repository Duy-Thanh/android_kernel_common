// SPDX-License-Identifier: GPL-2.0

#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/module.h>

#include "helpers.h"

bool my_get_close_on_exec(int fd) {
    struct files_struct *files = current->files;
    bool res = false;

    rcu_read_lock();

    if (fd >= 0 && fd < rcu_dereference(files->fdt)->max_fds) {
        res = close_on_exec(fd, files);
    }

    rcu_read_unlock();

    return res;
}

void my_set_close_on_exec(int fd, bool flag) {
    struct files_struct *files = current->files;
    struct fdtable *fdt;

    spin_lock(&files->file_lock);

    fdt = files_fdtable(files);

    if (fd >= 0 && fd < fdt->max_fds) {
        if (flag) {
            __set_bit(fd, fdt->close_on_exec);
        } else {
            __clear_bit(fd, fdt->close_on_exec);
        }
    }

    spin_unlock(&files->file_lock);
}

struct inode *my_file_inode(const struct file *f) {
    return file_inode(f);
}

void my_i_size_write(struct inode* inode, loff_t i_size) {
    i_size_write(inode, i_size);
}

loff_t my_i_size_read(const struct inode *inode) {
    return i_size_read(inode);
}

void my_i_mmap_lock_read(void* mapping) {
    i_mmap_lock_read(mapping);
}

void my_i_mmap_unlock_read(void* mapping) {
    i_mmap_unlock_read(mapping);
}

bool my_rb_empty_root(const struct rb_root* root) {
    return RB_EMPTY_ROOT(root) != 0;
}

void my_init_sync_kiocb(struct kiocb *kiocb, struct file *filp) {
    init_sync_kiocb(kiocb, filp);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WrapFD");
