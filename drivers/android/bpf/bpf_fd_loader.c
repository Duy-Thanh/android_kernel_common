// SPDX-License-Identifier: GPL-2.0-only
/*
 * bpf_fd_loader.c - Kernel module exposing an IOCTL interface to userspace
 *                   for loading and managing BPF file descriptors.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/bpf.h>
#include <linux/file.h>
#include <linux/ioctl.h>
#include <linux/kernel_read_file.h>
#include <linux/fdtable.h>
#include <linux/vmalloc.h>

#define DEVICE_NAME "bpf_fd_loader"

/* IOCTL Definitions */
#define BPF_FD_LOADER_MAGIC 'B'

struct bpf_fd_loader_arg {
	__u32		loader_fd;
	__aligned_u64	ctx;
	__u32		ctx_size;
};

#define BPF_FD_LOADER_IOCTL_PING \
	_IO(BPF_FD_LOADER_MAGIC, 1)

#define BPF_FD_LOADER_IOCTL_RECEIVE_FD \
	_IOWR(BPF_FD_LOADER_MAGIC, 2, struct bpf_fd_loader_arg)

#define BPF_LOADER_PROG_SEC "__loader.prog"
#define BPF_LOADER_MAP_SEC "__loader.map"
#define BPF_LOADER_LICENSE_SEC "license"
#define BPF_LOADER_MAX_SIZE 8U << 20 /* 8MB */

struct elf_info {
	Elf64_Ehdr *hdr;
	unsigned long len;
	Elf64_Shdr *sechdrs;
	char *secstrings;
};

int kern_sys_bpf(int cmd, union bpf_attr *attr, unsigned int size);

static int bpf_validate_section_offset(const struct elf_info *info, Elf64_Shdr *shdr)
{
	unsigned long long secend;

	/*
	 * Check for both overflow and offset/size being
	 * too large.
	 */
	secend = shdr->sh_offset + shdr->sh_size;
	if (secend < shdr->sh_offset || secend > info->len)
		return -ENOEXEC;

	return 0;
}

static int bpf_elf_validity_ehdr(const struct elf_info *info)
{
	if (info->len < sizeof(*(info->hdr))) {
		pr_err("Invalid ELF header len %lu\n", info->len);
		return -ENOEXEC;
	}
	if (memcmp(info->hdr->e_ident, ELFMAG, SELFMAG) != 0) {
		pr_err("Invalid ELF header magic: != %s\n", ELFMAG);
		return -ENOEXEC;
	}
	if (info->hdr->e_ident[EI_CLASS] != ELFCLASS64) {
		pr_err("Only 64-bit ELF is supported\n");
		return -ENOEXEC;
	}
	if (info->hdr->e_type != ET_REL) {
		pr_err("Invalid ELF header type: %u != %u\n",
		       info->hdr->e_type, ET_REL);
		return -ENOEXEC;
	}
	if (info->hdr->e_machine != EM_BPF) {
		pr_err("Invalid ELF machine type: %u != %u\n",
		       info->hdr->e_machine, EM_BPF);
		return -ENOEXEC;
	}
	return 0;
}

static int bpf_elf_validity_cache_sechdrs(struct elf_info *info)
{
	Elf64_Shdr *sechdrs;
	Elf64_Shdr *shdr;
	int i;
	int err;

	err = bpf_elf_validity_ehdr(info);
	if (err < 0)
		return err;

	if (info->hdr->e_shentsize != sizeof(Elf64_Shdr)) {
		pr_err("Invalid ELF section header size\n");
		return -ENOEXEC;
	}

	/*
	 * e_shnum is 16 bits, and sizeof(Elf64_Shdr) is
	 * known and small. So e_shnum * sizeof(Elf64_Shdr)
	 * will not overflow unsigned long on any platform.
	 */
	if (info->hdr->e_shoff >= info->len
	    || (info->hdr->e_shnum * sizeof(Elf64_Shdr) >
		info->len - info->hdr->e_shoff)) {
		pr_err("Invalid ELF section header overflow\n");
		return -ENOEXEC;
	}

	sechdrs = (void *)info->hdr + info->hdr->e_shoff;

	/*
	 * The code assumes that section 0 has a length of zero and
	 * an addr of zero, so check for it.
	 */
	if (sechdrs[0].sh_type != SHT_NULL
	    || sechdrs[0].sh_size != 0
	    || sechdrs[0].sh_addr != 0) {
		pr_err("ELF Spec violation: section 0 type(%d)!=SH_NULL or non-zero len or addr\n",
		       sechdrs[0].sh_type);
		return -ENOEXEC;
	}

	/* Validate contents are inbounds */
	for (i = 1; i < info->hdr->e_shnum; i++) {
		shdr = &sechdrs[i];
		switch (shdr->sh_type) {
		case SHT_NULL:
		case SHT_NOBITS:
			/* No contents, offset/size don't mean anything */
			continue;
		default:
			err = bpf_validate_section_offset(info, shdr);
			if (err < 0) {
				pr_err("Invalid ELF section in BPF loader (section %u type %u)\n",
				       i, shdr->sh_type);
				return err;
			}
		}
	}

	info->sechdrs = sechdrs;

	return 0;
}

static int bpf_elf_validity_cache_secstrings(struct elf_info *info)
{
	Elf64_Shdr *strhdr, *shdr;
	char *secstrings;
	int i;

	/*
	 * Verify if the section name table index is valid.
	 */
	if (info->hdr->e_shstrndx == SHN_UNDEF
	    || info->hdr->e_shstrndx >= info->hdr->e_shnum) {
		pr_err("Invalid ELF section name index: %d || e_shstrndx (%d) >= e_shnum (%d)\n",
		       info->hdr->e_shstrndx, info->hdr->e_shstrndx,
		       info->hdr->e_shnum);
		return -ENOEXEC;
	}

	strhdr = &info->sechdrs[info->hdr->e_shstrndx];

	if (strhdr->sh_type != SHT_STRTAB) {
		pr_err("Invalid ELF section name table type: %u\n", strhdr->sh_type);
		return -ENOEXEC;
	}

	/*
	 * The section name table must be NUL-terminated, as required
	 * by the spec. This makes strcmp and pr_* calls that access
	 * strings in the section safe.
	 */
	secstrings = (void *)info->hdr + strhdr->sh_offset;
	if (strhdr->sh_size == 0) {
		pr_err("empty section name table\n");
		return -ENOEXEC;
	}
	if (secstrings[strhdr->sh_size - 1] != '\0') {
		pr_err("ELF Spec violation: section name table isn't null terminated\n");
		return -ENOEXEC;
	}

	for (i = 0; i < info->hdr->e_shnum; i++) {
		shdr = &info->sechdrs[i];
		/* SHT_NULL means sh_name has an undefined value */
		if (shdr->sh_type == SHT_NULL)
			continue;
		if (shdr->sh_name >= strhdr->sh_size) {
			pr_err("Invalid ELF section name in BPF loader (section %u type %u)\n",
			       i, shdr->sh_type);
			return -ENOEXEC;
		}
	}

	info->secstrings = secstrings;
	return 0;
}

static int find_elf_section(const struct elf_info *info,
			       const char *sect_name, void **sect, int *sect_sz)
{
	Elf64_Shdr *shdr;

	for (int i = 1; i < info->hdr->e_shnum; i++) {
		shdr = &info->sechdrs[i];
		if (shdr->sh_type == SHT_NULL || shdr->sh_type == SHT_NOBITS)
			continue;
		if (strcmp(sect_name, info->secstrings + shdr->sh_name) == 0) {
			*sect = (void *)info->hdr + shdr->sh_offset;
			*sect_sz = shdr->sh_size;
			return 0;
		}
	}

	return -EINVAL;
}

static int loader_load_fd(struct bpf_fd_loader_arg *args)
{
	void *buf = NULL, *insns = NULL, *data = NULL, *license = NULL;
	void *kctx = NULL;
	int len, err = 0;
	int insns_sz = 0, data_sz = 0, license_sz = 0;
	int map_fd, prog_fd;
	size_t ctx_sz;
	union bpf_attr sattr = { 0 };
	unsigned int zero = 0;

	if (args->ctx_size > U16_MAX)
		return -EINVAL;

	CLASS(fd, f)(args->loader_fd);
	if (fd_empty(f))
		return -EINVAL;

	len = kernel_read_file(fd_file(f), 0, &buf, BPF_LOADER_MAX_SIZE, NULL,
			       READING_MODULE);
	if (len < 0) {
		err = len;
		goto out;
	}

	struct elf_info elf_info = {
		.hdr = (Elf64_Ehdr *) buf,
		.len = len,
	};

	err = bpf_elf_validity_cache_sechdrs(&elf_info);
	if (err)
		goto out_free_buf;

	err = bpf_elf_validity_cache_secstrings(&elf_info);
	if (err)
		goto out_free_buf;

	err = find_elf_section(&elf_info, BPF_LOADER_PROG_SEC, &insns, &insns_sz);
	if (err)
		goto out_free_buf;

	err = find_elf_section(&elf_info, BPF_LOADER_MAP_SEC, &data, &data_sz);
	if (err)
		goto out_free_buf;

	err = find_elf_section(&elf_info, BPF_LOADER_LICENSE_SEC, &license, &license_sz);
	if (err)
		goto out_free_buf;

	if (license_sz == 0 || ((char *)license)[license_sz - 1] != '\0') {
		pr_err("ELF Spec violation: license section isn't null terminated\n");
		err = -ENOEXEC;
		goto out_free_buf;
	}

	memset(&sattr, 0, sizeof(sattr));
	sattr.map_type = BPF_MAP_TYPE_ARRAY;
	sattr.key_size = sizeof(unsigned int);
	sattr.value_size = data_sz;
	sattr.max_entries = 1;
	map_fd = kern_sys_bpf(BPF_MAP_CREATE, &sattr, sizeof(sattr));
	if (map_fd < 0) {
		err = map_fd;
		goto out_free_buf;
	}

	memset(&sattr, 0, sizeof(sattr));
	sattr.map_fd = map_fd;
	sattr.key = (unsigned long) &zero;
	sattr.value = (unsigned long) data;
	err = kern_sys_bpf(BPF_MAP_UPDATE_ELEM, &sattr, sizeof(sattr));
	if (err < 0)
		goto close_map_err;

	memset(&sattr, 0, sizeof(sattr));
	sattr.prog_type = BPF_PROG_TYPE_SYSCALL;
	sattr.license = (unsigned long) license;
	sattr.insns = (unsigned long) insns;
	sattr.insn_cnt = insns_sz / sizeof(struct bpf_insn);
	sattr.fd_array = (unsigned long) &map_fd;
	sattr.prog_flags = BPF_F_SLEEPABLE;
	strscpy(sattr.prog_name, BPF_LOADER_PROG_SEC, sizeof(BPF_LOADER_PROG_SEC));
	prog_fd = kern_sys_bpf(BPF_PROG_LOAD, &sattr, sizeof(sattr));
	if (prog_fd < 0) {
		err = prog_fd;
		goto close_map_err;
	}

	memset(&sattr, 0, sizeof(sattr));
	ctx_sz = args->ctx_size;
	kctx = kzalloc(ctx_sz, GFP_KERNEL);
	if (kctx == NULL) {
		err = -ENOMEM;
		goto close_prog_err;
	}
	sattr.test.prog_fd = prog_fd;
	sattr.test.ctx_in = (unsigned long) kctx;
	sattr.test.ctx_size_in = ctx_sz;
	err = kern_sys_bpf(BPF_PROG_TEST_RUN, &sattr, sizeof(sattr));
	if (err < 0)
		goto free_ctx;
	err = sattr.test.retval;
	if (err < 0)
		goto free_ctx;

	if (copy_to_user((void *) args->ctx, kctx, ctx_sz) != 0)
		err = -EFAULT;

free_ctx:
	kfree(kctx);
close_prog_err:
	close_fd(prog_fd);
close_map_err:
	close_fd(map_fd);
out_free_buf:
	vfree(buf);
out:
	return err;
}

static int bpf_fd_loader_open(struct inode *inode, struct file *file)
{
	pr_info("bpf_fd_loader: device opened\n");
	return 0;
}

static int bpf_fd_loader_release(struct inode *inode, struct file *file)
{
	pr_info("bpf_fd_loader: device closed\n");
	return 0;
}

static long bpf_fd_loader_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bpf_fd_loader_arg loader_arg;

	switch (cmd) {
	case BPF_FD_LOADER_IOCTL_PING:
		pr_info("bpf_fd_loader: IOCTL PING received\n");
		return 0;

	case BPF_FD_LOADER_IOCTL_RECEIVE_FD:
		if (copy_from_user(&loader_arg, (void __user *)arg, sizeof(loader_arg)))
			return -EFAULT;

		pr_info("bpf_fd_loader: received loader_fd = %u, ctx_size = %u\n",
			loader_arg.loader_fd, loader_arg.ctx_size);

		int ret = loader_load_fd(&loader_arg);
		if (ret < 0)
			return ret;

		if (copy_to_user((void __user *)arg, &loader_arg, sizeof(loader_arg)))
			return -EFAULT;
		return 0;

	default:
		pr_warn("bpf_fd_loader: unknown IOCTL cmd 0x%x\n", cmd);
		return -EINVAL;
	}
}

#ifdef CONFIG_COMPAT
static long bpf_fd_loader_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return bpf_fd_loader_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static const struct file_operations bpf_fd_loader_fops = {
	.owner          = THIS_MODULE,
	.open           = bpf_fd_loader_open,
	.release        = bpf_fd_loader_release,
	.unlocked_ioctl = bpf_fd_loader_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = bpf_fd_loader_compat_ioctl,
#endif
};

static struct miscdevice bpf_fd_loader_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = DEVICE_NAME,
	.fops  = &bpf_fd_loader_fops,
	.mode  = 0666,
};

static int __init bpf_fd_loader_init(void)
{
	int ret;

	ret = misc_register(&bpf_fd_loader_miscdev);
	if (ret) {
		pr_err("bpf_fd_loader: failed to register misc device (ret=%d)\n", ret);
		return ret;
	}

	pr_info("bpf_fd_loader: module loaded, device node /dev/%s created\n", DEVICE_NAME);
	return 0;
}

static void __exit bpf_fd_loader_exit(void)
{
	misc_deregister(&bpf_fd_loader_miscdev);
	pr_info("bpf_fd_loader: module unloaded\n");
}

module_init(bpf_fd_loader_init);
module_exit(bpf_fd_loader_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thiébaud Weksteen <tweek@google.com>");
MODULE_AUTHOR("Siddharth Nayyar <sidnayyar@google.com>");
MODULE_DESCRIPTION("BPF FD Loader Kernel Module with IOCTL Interface");
MODULE_IMPORT_NS("BPF_INTERNAL");
MODULE_IMPORT_NS("ANDROID_GKI_VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver");
