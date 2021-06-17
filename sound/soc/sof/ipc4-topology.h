/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2020 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_TOPOLOGY2_H__
#define __INCLUDE_SOUND_SOF_TOPOLOGY2_H__

#include <sound/sof/header2.h>

struct sof_copier_gateway_cfg {
	uint32_t node_id; /**< ID of Gateway Node */

	uint32_t dma_buffer_size; /**< Preferred Gateway DMA buffer size (in bytes) */

	/* Length of gateway node configuration blob specified in #config_data */
	uint32_t config_length;

	uint32_t config_data[0]; /**< Gateway node configuration blob */
};

struct sof_ipc4_module_copier {
	struct basic_module_cfg base_config; /**< common config for all comps */
	struct audio_format out_format;
	uint32_t copier_feature_mask;
	struct sof_copier_gateway_cfg gtw_cfg;
} __packed;

struct sof_ipc4_pipeline {
	struct sof_ipc_pipe_new pipe_new;
	uint32_t lp_mode;	/**< low power mode */
	uint32_t mem_usage;
	int state;
};

struct sof_ipc4_host {
	struct snd_soc_component *scomp;
	struct sof_ipc4_module_copier copier;
	u32 *copier_config;
	uint32_t ipc_config_size;
	void *ipc_config_data;
};

struct sof_ipc4_dai {
	struct snd_sof_dai dai;
	struct sof_ipc4_module_copier copier;
	uint32_t *copier_config;
	uint32_t ipc_config_size;
	void *ipc_config_data;
};

int snd_sof_load_topology2(struct snd_soc_component *scomp, const char *file);
#endif
