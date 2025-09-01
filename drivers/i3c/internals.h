/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Cadence Design Systems Inc.
 *
 * Author: Boris Brezillon <boris.brezillon@bootlin.com>
 */

#ifndef I3C_INTERNALS_H
#define I3C_INTERNALS_H

#include <linux/i3c/master.h>
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#include <linux/io.h>
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

void i3c_bus_normaluse_lock(struct i3c_bus *bus);
void i3c_bus_normaluse_unlock(struct i3c_bus *bus);

int i3c_dev_setdasa_locked(struct i3c_dev_desc *dev);
int i3c_dev_do_priv_xfers_locked(struct i3c_dev_desc *dev,
				 struct i3c_priv_xfer *xfers,
				 int nxfers);
int i3c_dev_disable_ibi_locked(struct i3c_dev_desc *dev);
int i3c_dev_enable_ibi_locked(struct i3c_dev_desc *dev);
int i3c_dev_request_ibi_locked(struct i3c_dev_desc *dev,
			       const struct i3c_ibi_setup *req);
void i3c_dev_free_ibi_locked(struct i3c_dev_desc *dev);
#endif /* I3C_INTERNAL_H */
