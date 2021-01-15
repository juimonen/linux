/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 *
 * Author: Rander Wang <rander.wang@linux.intel.com>
 */

#ifndef __SOF_IPC4_MODULES_H
#define __SOF_IPC4_MODULES_H

#include <sound/sof/stream.h>
#include "sof-priv.h"
#include "sof-audio.h"

#define SOF_IPC4_COMP_ID(x, y) ((x) << 16 | (y))
#define SOF_IPC4_MODULE_ID(x) (((x) >> 16) & 0xFFFF)
#define SOF_IPC4_INSTANCE_ID(x) ((x) & 0xFFFF)
#define SOF_IPC4_MAX_INST_ID 255

#define SOF_IPC4_FW_PAGE_SIZE BIT(12)
#define SOF_IPC4_FW_PAGE(x) ((((x) + BIT(12) - 1) & ~(BIT(12) - 1)) >> 12)
#define SOF_IPC4_FW_ROUNDUP(x) (((x) + BIT(6) - 1) & (~(BIT(6) - 1)))

#define SOF_IPC4_MODULE_LL	BIT(5)
#define SOF_IPC4_MODULE_INSTANCE_LIST_ITEM_SIZE 12
#define SOF_IPC4_PIPELINE_OBJECT_SIZE 448
#define SOF_IPC4_DATA_QUEUE_OBJECT_SIZE 128
#define SOF_IPC4_LL_TASK_OBJECT_SIZE 72
#define SOF_IPC4_DP_TASK_OBJECT_SIZE 104
#define SOF_IPC4_DP_TASK_LIST_SIZE (12 + 8)
#define SOF_IPC4_LL_TASK_LIST_ITEM_SIZE 12
#define SOF_IPC4_FW_MAX_PAGE_COUNT 20
#define SOF_IPC4_FW_MAX_QUEUE_COUNT 8

struct sof_channel_map_table {
	int ch_count;
	enum channel_config config;
	u32 ch_map;
};

struct sof_module_processor {
	u8 uuid[UUID_SIZE];
	int sch_num;
	int (*process)(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
		struct snd_sof_pcm *params, struct sof_ipc_pcm_params *ipc_params,
		int module_id, int instance_id, int lp_mode);
	int (*get_ipc_config)(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			      int *size, void **data);
	int (*get_mem_size)(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
					u32 *ibs, u32 *obs, u32 *bss);
};

int sof_ipc4_process_module(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			struct snd_sof_pcm *spcm, struct sof_ipc_pcm_params *pcm, int lp_mode);
int sof_ipc4_setup_module(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget, int lp_mode);
int sof_ipc4_get_module_mem_size(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget);
int sof_ipc4_release_module(struct snd_sof_dev *sdev, u32 module_id, u32 instance_id);
void sof_ipc4_update_module_info(struct snd_sof_dev *sdev);

#endif
