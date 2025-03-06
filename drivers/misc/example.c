#include <linux/errno.h>
#include <linux/module.h>

#include "example.h"

int misc_example_add(int left, int right)
{
        return left + right;
}
EXPORT_SYMBOL(misc_example_add);
