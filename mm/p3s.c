// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kstrtox.h>
#include <linux/mm_types.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/p3s/mm.h>

enum p3s_mode p3s_mode __ro_after_init = P3S_MODE_OFF;
EXPORT_SYMBOL(p3s_mode);

static int __init parse_p3s(char *str)
{
	unsigned int val;
	bool enabled;

	if (!str) {
		p3s_mode = P3S_MODE_ON;
		return 0;
	}

	if (!kstrtouint(str, 0, &val)) {
		if (val <= P3S_MODE_ALTERNATE) {
			p3s_mode = val;
			return 0;
		}
		return -EINVAL;
	}

	if (kstrtobool(str, &enabled))
		return -EINVAL;

	p3s_mode = enabled ? P3S_MODE_ON : P3S_MODE_OFF;
	return 0;
}
early_param("p3s", parse_p3s);
