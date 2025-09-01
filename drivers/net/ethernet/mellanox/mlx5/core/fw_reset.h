/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc.  All rights reserved. */

#ifndef __MLX5_FW_RESET_H
#define __MLX5_FW_RESET_H

#include "mlx5_core.h"

int mlx5_fw_reset_query(struct mlx5_core_dev *dev, u8 *reset_level, u8 *reset_type);
int mlx5_fw_reset_set_reset_sync(struct mlx5_core_dev *dev, u8 reset_type_sel,
				 struct netlink_ext_ack *extack);
int mlx5_fw_reset_set_live_patch(struct mlx5_core_dev *dev);

int mlx5_fw_reset_wait_reset_done(struct mlx5_core_dev *dev);
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
void mlx5_sync_reset_unload_flow(struct mlx5_core_dev *dev, bool locked);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
int mlx5_fw_reset_verify_fw_complete(struct mlx5_core_dev *dev,
				     struct netlink_ext_ack *extack);
void mlx5_fw_reset_events_start(struct mlx5_core_dev *dev);
void mlx5_fw_reset_events_stop(struct mlx5_core_dev *dev);
void mlx5_drain_fw_reset(struct mlx5_core_dev *dev);
int mlx5_fw_reset_init(struct mlx5_core_dev *dev);
void mlx5_fw_reset_cleanup(struct mlx5_core_dev *dev);

#endif
