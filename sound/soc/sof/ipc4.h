/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 *
 * Author: Rander Wang <rander.wang@linux.intel.com>
 */

#ifndef __SOF_IPC4_H
#define __SOF_IPC4_H

struct sof_ipc4_fw_status {
	int status;
	char *msg;
};

int sof_ipc4_create_pipeline(struct snd_sof_dev *sdev, u32 id, u32 memory_size,
					int priority, int lp_mode);
int sof_ipc4_set_pipeline_status(struct snd_sof_dev *sdev, u32 id, u32 status);
int sof_ipc4_delete_pipeline(struct snd_sof_dev *sdev, u32 id);

int sof_ipc4_initialize_module(struct snd_sof_dev *sdev, u32 mod_id, u32 instance_id,
				u32 param_size, u32 pipe_id, u32 core, u32 domain, void *data);
int sof_ipc4_bind_modules(struct snd_sof_dev *sdev, u32 mod_id, u32 instance_id,
			u32 src_queue,  u32 dst_mod, u32 dst_instance, u32 dst_queue);
int sof_ipc4_unbind_modules(struct snd_sof_dev *sdev, u32 mod_id, u32 instance_id,
			u32 src_queue,  u32 dst_mod, u32 dst_instance, u32 dst_queue);
int sof_ipc4_delete_module(struct snd_sof_dev *sdev, u32 mod_id, u32 instance_id);
int sof_ipc4_set_large_config_module(struct snd_sof_dev *sdev, u32 mod_id, u32 instance_id,
					u32 data_size, u32 param_id, void *data);

void sof_ipc4_check_reply_status(struct snd_sof_dev *sdev, u32 msg);

#endif // __SOF_IPC4_H
