// SPDX-License-Identifier: GPL-2.0
#include <linux/debugfs.h>
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#include <linux/seq_file.h>

#include <asm/ptdump.h>

static int ptdump_show(struct seq_file *m, void *v)
{
	struct ptdump_info *info = m->private;

	ptdump_walk(m, info);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
#include <linux/memory_hotplug.h>
#include <linux/seq_file.h>

#include <asm/ptdump.h>

static int ptdump_show(struct seq_file *m, void *v)
{
	struct ptdump_info *info = m->private;

	get_online_mems();
	ptdump_walk(m, info);
	put_online_mems();
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ptdump);

void __init ptdump_debugfs_register(struct ptdump_info *info, const char *name)
{
	debugfs_create_file(name, 0400, NULL, info, &ptdump_fops);
}
