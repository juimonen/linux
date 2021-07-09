// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
// Author: Rander Wang <rander.wang@linux.intel.com>
//
// Generic module routines used to generate module params for ipc4
//

#include <sound/pcm_params.h>
#include "ipc4-modules.h"
#include "ipc4-topology.h"
#include "ipc4.h"
#include "ops.h"

struct sof_channel_map_table ch_map_table[] = {
	{ 1, CHANNEL_CONFIG_MONO, 0xFFFFFFF0 },
	{ 2, CHANNEL_CONFIG_STEREO, 0xFFFFFF10 },
	{ 3, CHANNEL_CONFIG_2_POINT_1, 0xFFFFF210 },
	{ 4, CHANNEL_CONFIG_3_POINT_1, 0xFFFF3210 },
	{ 5, CHANNEL_CONFIG_5_POINT_0, 0xFFF43210 },
	{ 6, CHANNEL_CONFIG_5_POINT_1, 0xFF543210 },
	{ 7, CHANNEL_CONFIG_INVALID, 0xFFFFFFFF },
	{ 8, CHANNEL_CONFIG_7_POINT_1, 0x76543210 },
};

static int sof_ipc4_init_audio_fmt(struct snd_sof_dev *sdev,
				   struct audio_format *audio_fmt,
				   int channels, int rate, int width,
				   int valid_bit_depth)
{
	audio_fmt->channels_count = channels;
	audio_fmt->sampling_frequency = rate;
	audio_fmt->bit_depth = width;
	audio_fmt->interleaving_style = CHANNELS_INTERLEAVED;
	audio_fmt->valid_bit_depth = valid_bit_depth;
	audio_fmt->s_type = MSB_INTEGER;


	if (ch_map_table[channels].config == CHANNEL_CONFIG_INVALID ||
		channels >= ARRAY_SIZE(ch_map_table)) {
		dev_err(sdev->dev, "unsupported change count %d", channels);
		return -EINVAL;
	}

	audio_fmt->ch_cfg = ch_map_table[channels - 1].config;
	audio_fmt->ch_map = ch_map_table[channels - 1].ch_map;

	return 0;
}

static int sof_ipc4_init_base_config(struct snd_sof_dev *sdev,
						  struct snd_sof_widget *swidget,
					      struct basic_module_cfg *base_config,
					      struct snd_pcm_hw_params *params,
					      struct sof_ipc_stream_params *ipc_params)
{
	struct sof_module_processor *processor;
	int channels, rate, width;
	int module_id;

	module_id = SOF_IPC4_MODULE_ID(swidget->comp_id);
	processor = sdev->fw_modules[module_id].private;

	width = (ipc_params->sample_container_bytes << 3);
	rate = ipc_params->rate;
	channels = ipc_params->channels;

	dev_dbg(sdev->dev, "format width %d, rate %d, ch %d", width, rate, channels);

	/* byte based */
	base_config->ibs = sof_ipc4_module_buffer_size(channels, rate, width, processor->sch_num);
	base_config->obs = sof_ipc4_module_buffer_size(channels, rate, width, processor->sch_num);
	base_config->is_pages = SOF_IPC4_FW_PAGE(sdev->fw_modules[module_id].bss_size);

	return sof_ipc4_init_audio_fmt(sdev, &base_config->audio_fmt, channels,
				       rate, width, params_width(params));
}

static int sof_ipc4_init_out_audio_fmt(struct snd_sof_dev *sdev,
					       struct sof_ipc4_module_copier *copier,
					       struct snd_pcm_hw_params *params)
{
	int channels, rate, width;

	width = params_width(params);
	rate = params_rate(params);
	channels = params_channels(params);
	return sof_ipc4_init_audio_fmt(sdev, &copier->out_format, channels, rate,
				       width, width);
}

static int sof_ipc4_process_copier_module(struct snd_sof_dev *sdev,
					  struct snd_sof_widget *swidget,
					  struct snd_sof_pcm *spcm,
					  struct sof_ipc_pcm_params *pcm,
					  int module_id,
					  int instance_id,
					  int lp_mode)
{
	struct sof_ipc4_module_copier *copier;
	struct snd_pcm_hw_params *params;
	int type = swidget->id;
	void **ipc_config_data;
	int *ipc_config_size;
	uint32_t **data;
	int param_size;
	int ret;

	dev_dbg(sdev->dev, "copier widget %s, type %d", swidget->widget->name, type);

	if (type == snd_soc_dapm_aif_in || type == snd_soc_dapm_aif_out) {
		struct sof_ipc4_host *host;

		host = (struct sof_ipc4_host *)swidget->private;
		copier = &host->copier;
		data = &host->copier_config;
		ipc_config_size = &host->ipc_config_size;
		ipc_config_data = &host->ipc_config_data;
	} else if (type == snd_soc_dapm_dai_in || type == snd_soc_dapm_dai_out) {
		struct sof_ipc4_dai *ipc4_dai;

		ipc4_dai = (struct sof_ipc4_dai *)swidget->private;
		copier = &ipc4_dai->copier;
		data = &ipc4_dai->copier_config;
		ipc_config_size = &ipc4_dai->ipc_config_size;
		ipc_config_data = &ipc4_dai->ipc_config_data;
	} else {
		dev_err(sdev->dev, "error: current type %d of copier is not supported now", type);
		return -EINVAL;
	}

	params = &spcm->params[pcm->params.direction];
	ret = sof_ipc4_init_base_config(sdev, swidget, &copier->base_config, params, &pcm->params);
	if (ret < 0)
		return ret;

	ret = sof_ipc4_init_out_audio_fmt(sdev, copier, params);
	if (ret < 0)
		return ret;

	ret = sof_get_module_config(sdev, params, pcm, swidget);
	if (ret) {
		dev_err(sdev->dev, "error: failed to get config for widget %s",
			swidget->widget->name);
		return ret;
	}

	/* config_length is DWORD based */
	param_size = sizeof(*copier) + copier->gtw_cfg.config_length * 4;

	dev_dbg(sdev->dev, "module %s param size is %d", swidget->widget->name, param_size);

	*ipc_config_data = devm_kzalloc(sdev->dev, param_size, GFP_KERNEL);
	if (!*ipc_config_data)
		return -ENOMEM;

	*ipc_config_size = param_size;

	memcpy(*ipc_config_data, (void *)copier, sizeof(*copier));
	if (copier->gtw_cfg.config_length)
		memcpy(*ipc_config_data + sizeof(*copier),
		       *data, copier->gtw_cfg.config_length * 4);

	return 0;
}

int sof_ipc4_get_copier_config(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			       int *size, void **data)
{
	int type = swidget->id;

	if (type == snd_soc_dapm_aif_in || type == snd_soc_dapm_aif_out) {
		struct sof_ipc4_host *host;

		host = (struct sof_ipc4_host *)swidget->private;
		*size = host->ipc_config_size;
		*data = host->ipc_config_data;
	} else if (type == snd_soc_dapm_dai_in || type == snd_soc_dapm_dai_out) {
		struct sof_ipc4_dai *ipc4_dai;

		ipc4_dai = (struct sof_ipc4_dai *)swidget->private;
		*size = ipc4_dai->ipc_config_size;
		*data = ipc4_dai->ipc_config_data;
	} else {
		dev_err(sdev->dev, "error: current type %d of copier is not supported now", type);
		return -EINVAL;
	}

	return 0;
}

int sof_ipc4_get_copier_mem_size(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
					u32 *ibs, u32 *obs, u32 *bss)
{
	struct sof_ipc4_module_copier *copier;
	int type = swidget->id;

	if (type == snd_soc_dapm_aif_in || type == snd_soc_dapm_aif_out) {
		struct sof_ipc4_host *host;

		host = (struct sof_ipc4_host *)swidget->private;
		copier = &host->copier;
	} else if (type == snd_soc_dapm_dai_in || type == snd_soc_dapm_dai_out) {
		struct sof_ipc4_dai *ipc4_dai;

		ipc4_dai = (struct sof_ipc4_dai *)swidget->private;
		copier = &ipc4_dai->copier;
	} else {
		dev_err(sdev->dev, "error: current type %d of copier is not supported now", type);
		return -EINVAL;
	}

	*ibs = copier->base_config.ibs;
	*obs = copier->base_config.obs;
	*bss = copier->base_config.is_pages;

	return 0;
}

struct sof_module_processor module_processor[] = {
	{
		{0x83, 0xC, 0xA0, 0x9B, 0x12, 0xCA, 0x83, 0x4A,
		 0x94, 0x3C, 0x1F, 0xA2, 0xE8, 0x2F, 0x9D, 0xDA},
		2,
		sof_ipc4_process_copier_module,
		sof_ipc4_get_copier_config,
		sof_ipc4_get_copier_mem_size
	},
};

void sof_ipc4_update_module_info(struct snd_sof_dev *sdev)
{
	struct sof_ipc4_fw_modules *module_entry;
	int i, j;

	module_entry = sdev->fw_modules;
	for (i = 0; i < sdev->fw_module_num; i++) {
		for (j = 0; j < sizeof(module_processor) /
				sizeof(struct sof_module_processor); j++) {
			if (memcmp(&module_processor[j].uuid, module_entry[i].uuid, UUID_SIZE))
				continue;

			module_entry[i].private = &module_processor[j];
			break;
		}
	}
}
EXPORT_SYMBOL(sof_ipc4_update_module_info);

static int sof_get_instance_id(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			       int module_id)
{
	int i, *instance_id;

	instance_id = sdev->fw_modules[module_id].instance_id;

	/* find unused bit */
	spin_lock_irq(&sdev->ipc_lock);
	for (i = 1; i <= sdev->fw_modules[module_id].instance_max_count; i++) {
		if (!(BIT(i % 32) & instance_id[i / 32])) {
			instance_id[i / 32] |= BIT(i % 32);
			break;
		}
	}
	spin_unlock_irq(&sdev->ipc_lock);

	if (i <= SOF_IPC4_MAX_INST_ID)
		return i;

	return -EBUSY;
}

static void sof_put_instance_id(struct snd_sof_dev *sdev, int module_id, int instance_id)
{
	spin_lock_irq(&sdev->ipc_lock);
	sdev->fw_modules[module_id].instance_id[instance_id / 32] &= ~BIT(instance_id % 32);
	spin_unlock_irq(&sdev->ipc_lock);
}

int sof_ipc4_process_module(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			struct snd_sof_pcm *spcm, struct sof_ipc_pcm_params *pcm, int lp_mode)
{
	struct sof_module_processor *processor;
	int module_id, instance_id;
	int i, ret;

	for (i = 0; i < sdev->fw_module_num; i++) {
		if (!memcmp(swidget->comp_ext.uuid, sdev->fw_modules[i].uuid, UUID_SIZE)) {
			module_id = i;
			break;
		}
	}

	if (i >= sdev->fw_module_num) {
		dev_err(sdev->dev, "can't find module %s with uuid %pUL", swidget->widget->name,
				swidget->comp_ext.uuid);
		return -EINVAL;
	}

	instance_id = sof_get_instance_id(sdev, swidget, module_id);
	if (instance_id < 0) {
		dev_err(sdev->dev, "failed to get instatnce id for widget %s, module id %d",
				swidget->widget->name, module_id);
		return instance_id;
	}

	swidget->comp_id = SOF_IPC4_COMP_ID(module_id, instance_id);
	dev_dbg(sdev->dev, "widget %s, comp id %x", swidget->widget->name, swidget->comp_id);

	processor = sdev->fw_modules[i].private;
	ret = processor->process(sdev, swidget, spcm, pcm, module_id, instance_id, lp_mode);
	if (ret < 0)
		sof_put_instance_id(sdev, module_id, instance_id);

	return ret;
}

int sof_ipc4_setup_module(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget, int lp_mode)
{
	struct sof_module_processor *processor;
	int module_id, instance_id;
	uint32_t config_size;
	void *config_data;
	int ret;

	module_id = SOF_IPC4_MODULE_ID(swidget->comp_id);
	instance_id = SOF_IPC4_INSTANCE_ID(swidget->comp_id);
	processor = sdev->fw_modules[module_id].private;
	ret = processor->get_ipc_config(sdev, swidget, &config_size, &config_data);
	if (ret < 0)
		return ret;

	ret = sof_ipc4_initialize_module(sdev, module_id, instance_id,  config_size,
				swidget->pipeline_id, swidget->core, lp_mode, config_data);

	return ret;
}

int sof_ipc4_get_module_mem_size(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget)
{
	struct sof_module_processor *processor;
	int task_mem, queue_mem;
	int ibs, obs, bss, total;
	int module_id;
	int ret;

	module_id = SOF_IPC4_MODULE_ID(swidget->comp_id);
	processor = sdev->fw_modules[module_id].private;
	ret = processor->get_mem_size(sdev, swidget, &ibs, &obs, &bss);
	if (ret < 0)
		return ret;

	task_mem = SOF_IPC4_PIPELINE_OBJECT_SIZE;
	task_mem += SOF_IPC4_MODULE_INSTANCE_LIST_ITEM_SIZE + bss;

	if (sdev->fw_modules[module_id].type & SOF_IPC4_MODULE_LL) {
		task_mem += SOF_IPC4_FW_ROUNDUP(SOF_IPC4_LL_TASK_OBJECT_SIZE);
		task_mem += SOF_IPC4_FW_MAX_QUEUE_COUNT * SOF_IPC4_MODULE_INSTANCE_LIST_ITEM_SIZE;
		task_mem += SOF_IPC4_LL_TASK_LIST_ITEM_SIZE;
	} else {
		task_mem += SOF_IPC4_FW_ROUNDUP(SOF_IPC4_DP_TASK_OBJECT_SIZE);
		task_mem += SOF_IPC4_DP_TASK_LIST_SIZE;
	}

	ibs = SOF_IPC4_FW_ROUNDUP(ibs);
	queue_mem = SOF_IPC4_FW_MAX_QUEUE_COUNT * (SOF_IPC4_DATA_QUEUE_OBJECT_SIZE +  4 * ibs);

	total = SOF_IPC4_FW_PAGE(task_mem + queue_mem);
	if (total > SOF_IPC4_FW_MAX_PAGE_COUNT) {
		dev_info(sdev->dev, "task memory usage %d, queue memory usage %d",
			 task_mem, queue_mem);
		total = SOF_IPC4_FW_MAX_PAGE_COUNT;
	}

	return total;
}

/*
 * module in fw has been deleted when deleting pipeline in fw,
 * so here only release instance id in driver
 */
int sof_ipc4_release_module(struct snd_sof_dev *sdev, u32 module_id, u32 instance_id)
{
	sof_put_instance_id(sdev, module_id, instance_id);

	return 0;
}

