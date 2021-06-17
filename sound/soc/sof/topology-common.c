// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
// Author: Rander Wang <rander.wang@linux.intel.com>
//

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>
#include <uapi/sound/sof/tokens.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ops.h"
#include "topology-common.h"

static const struct sof_frame_types sof_frames[] = {
	{"s16le", SOF_IPC_FRAME_S16_LE},
	{"s24le", SOF_IPC_FRAME_S24_4LE},
	{"s32le", SOF_IPC_FRAME_S32_LE},
	{"float", SOF_IPC_FRAME_FLOAT},
};

static const struct sof_dai_types sof_dais[] = {
	{"SSP", SOF_DAI_INTEL_SSP},
	{"HDA", SOF_DAI_INTEL_HDA},
	{"DMIC", SOF_DAI_INTEL_DMIC},
	{"ALH", SOF_DAI_INTEL_ALH},
	{"SAI", SOF_DAI_IMX_SAI},
	{"ESAI", SOF_DAI_IMX_ESAI},
};

/* Component extended tokens */
static const struct sof_topology_token comp_ext_tokens[] = {
	{SOF_TKN_COMP_UUID,
		SND_SOC_TPLG_TUPLE_TYPE_UUID, get_token_uuid,
		offsetof(struct sof_ipc_comp_ext, uuid), 0},
};

/* DAI */
static const struct sof_topology_token dai_tokens[] = {
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct sof_ipc_comp_dai, type), 0},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_dai, dai_index), 0},
	{SOF_TKN_DAI_DIRECTION, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_comp_dai, direction), 0},
};

/* BE DAI link */
static const struct sof_topology_token dai_link_tokens[] = {
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct sof_ipc_dai_config, type), 0},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_config, dai_index), 0},
};

/* scheduling */
static const struct sof_topology_token sched_tokens[] = {
	{SOF_TKN_SCHED_PERIOD, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, period), 0},
	{SOF_TKN_SCHED_PRIORITY, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, priority), 0},
	{SOF_TKN_SCHED_MIPS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, period_mips), 0},
	{SOF_TKN_SCHED_CORE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, core), 0},
	{SOF_TKN_SCHED_FRAMES, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, frames_per_sched), 0},
	{SOF_TKN_SCHED_TIME_DOMAIN, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_pipe_new, time_domain), 0},
};

/* SSP */
static const struct sof_topology_token ssp_tokens[] = {
	{SOF_TKN_INTEL_SSP_CLKS_CONTROL,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, clks_control), 0},
	{SOF_TKN_INTEL_SSP_MCLK_ID,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_ssp_params, mclk_id), 0},
	{SOF_TKN_INTEL_SSP_SAMPLE_BITS, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, sample_valid_bits), 0},
	{SOF_TKN_INTEL_SSP_FRAME_PULSE_WIDTH, SND_SOC_TPLG_TUPLE_TYPE_SHORT,
		get_token_u16,
		offsetof(struct sof_ipc_dai_ssp_params, frame_pulse_width), 0},
	{SOF_TKN_INTEL_SSP_QUIRKS, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, quirks), 0},
	{SOF_TKN_INTEL_SSP_TDM_PADDING_PER_SLOT, SND_SOC_TPLG_TUPLE_TYPE_BOOL,
		get_token_u16,
		offsetof(struct sof_ipc_dai_ssp_params,
			 tdm_per_slot_padding_flag), 0},
	{SOF_TKN_INTEL_SSP_BCLK_DELAY, SND_SOC_TPLG_TUPLE_TYPE_WORD,
		get_token_u32,
		offsetof(struct sof_ipc_dai_ssp_params, bclk_delay), 0},

};

/* DMIC */
static const struct sof_topology_token dmic_tokens[] = {
	{SOF_TKN_INTEL_DMIC_DRIVER_VERSION,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, driver_ipc_version),
		0},
	{SOF_TKN_INTEL_DMIC_CLK_MIN,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, pdmclk_min), 0},
	{SOF_TKN_INTEL_DMIC_CLK_MAX,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, pdmclk_max), 0},
	{SOF_TKN_INTEL_DMIC_SAMPLE_RATE,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, fifo_fs), 0},
	{SOF_TKN_INTEL_DMIC_DUTY_MIN,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_params, duty_min), 0},
	{SOF_TKN_INTEL_DMIC_DUTY_MAX,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_params, duty_max), 0},
	{SOF_TKN_INTEL_DMIC_NUM_PDM_ACTIVE,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params,
			 num_pdm_active), 0},
	{SOF_TKN_INTEL_DMIC_FIFO_WORD_LENGTH,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_params, fifo_bits), 0},
	{SOF_TKN_INTEL_DMIC_UNMUTE_RAMP_TIME_MS,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_dmic_params, unmute_ramp_time), 0},

};

/*
 * DMIC PDM Tokens
 * SOF_TKN_INTEL_DMIC_PDM_CTRL_ID should be the first token
 * as it increments the index while parsing the array of pdm tokens
 * and determines the correct offset
 */
static const struct sof_topology_token dmic_pdm_tokens[] = {
	{SOF_TKN_INTEL_DMIC_PDM_CTRL_ID,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, id),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_MIC_A_Enable,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, enable_mic_a),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_MIC_B_Enable,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, enable_mic_b),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_POLARITY_A,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, polarity_mic_a),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_POLARITY_B,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, polarity_mic_b),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_CLK_EDGE,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, clk_edge),
		0},
	{SOF_TKN_INTEL_DMIC_PDM_SKEW,
		SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_u16,
		offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, skew),
		0},
};

/* HDA */
static const struct sof_topology_token hda_tokens[] = {
	{SOF_TKN_INTEL_HDA_RATE,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_hda_params, rate), 0},
	{SOF_TKN_INTEL_HDA_CH,
		SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc_dai_hda_params, channels), 0},
};

static const struct sof_topology_token_entry token_table[] = {
	{"ext", ARRAY_SIZE(comp_ext_tokens), comp_ext_tokens},
	{"dai", ARRAY_SIZE(dai_tokens), dai_tokens},
	{"dai link", ARRAY_SIZE(dai_link_tokens), dai_link_tokens},
	{"dmic pdm", ARRAY_SIZE(dmic_pdm_tokens), dmic_pdm_tokens},
	{"dmic", ARRAY_SIZE(dmic_tokens), dmic_tokens},
	{"schedule", ARRAY_SIZE(sched_tokens), sched_tokens},
	{"ssp", ARRAY_SIZE(ssp_tokens), ssp_tokens},
};

enum sof_ipc_frame find_format(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_frames); i++) {
		if (strcmp(name, sof_frames[i].name) == 0)
			return sof_frames[i].frame;
	}

	/* use s32le if nothing is specified */
	return SOF_IPC_FRAME_S32_LE;
}

enum sof_ipc_dai_type find_dai(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_dais); i++) {
		if (strcmp(name, sof_dais[i].name) == 0)
			return sof_dais[i].type;
	}

	return SOF_DAI_INTEL_NONE;
}

int sof_parse_topology_tokens(struct snd_soc_component *scomp, void *object, int index,
			struct snd_soc_tplg_vendor_array *array, int priv_size)
{
	int ret;

	ret = sof_parse_tokens(scomp, object, token_table[index].token, token_table[index].size,
				   array, le32_to_cpu(priv_size));
	if (ret != 0)
		dev_err(scomp->dev, "error: parse %s tokens size %d failed %d\n",
			token_table[index].name, le32_to_cpu(priv_size), ret);

	return ret;
}

static int sof_parse_uuid_tokens(struct snd_soc_component *scomp,
				 void *object,
				 const struct sof_topology_token *tokens,
				 int count,
				 struct snd_soc_tplg_vendor_array *array,
				 size_t offset)
{
	struct snd_soc_tplg_vendor_uuid_elem *elem;
	int found = 0;
	int i, j;

	/* parse element by element */
	for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
		elem = &array->uuid[i];

		/* search for token */
		for (j = 0; j < count; j++) {
			/* match token type */
			if (tokens[j].type != SND_SOC_TPLG_TUPLE_TYPE_UUID)
				continue;

			/* match token id */
			if (tokens[j].token != le32_to_cpu(elem->token))
				continue;

			/* matched - now load token */
			tokens[j].get_token(elem, object,
					    offset + tokens[j].offset,
					    tokens[j].size);

			found++;
		}
	}

	return found;
}

static int sof_parse_string_tokens(struct snd_soc_component *scomp,
				   void *object,
				   const struct sof_topology_token *tokens,
				   int count,
				   struct snd_soc_tplg_vendor_array *array,
				   size_t offset)
{
	struct snd_soc_tplg_vendor_string_elem *elem;
	int found = 0;
	int i, j;

	/* parse element by element */
	for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
		elem = &array->string[i];

		/* search for token */
		for (j = 0; j < count; j++) {
			/* match token type */
			if (tokens[j].type != SND_SOC_TPLG_TUPLE_TYPE_STRING)
				continue;

			/* match token id */
			if (tokens[j].token != le32_to_cpu(elem->token))
				continue;

			/* matched - now load token */
			tokens[j].get_token(elem, object,
					    offset + tokens[j].offset,
					    tokens[j].size);

			found++;
		}
	}

	return found;
}

static int sof_parse_word_tokens(struct snd_soc_component *scomp,
				 void *object,
				 const struct sof_topology_token *tokens,
				 int count,
				 struct snd_soc_tplg_vendor_array *array,
				 size_t offset)
{
	struct snd_soc_tplg_vendor_value_elem *elem;
	int found = 0;
	int i, j;

	/* parse element by element */
	for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
		elem = &array->value[i];

		/* search for token */
		for (j = 0; j < count; j++) {
			/* match token type */
			if (!(tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_WORD ||
			      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_SHORT ||
			      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_BYTE ||
			      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_BOOL))
				continue;

			/* match token id */
			if (tokens[j].token != le32_to_cpu(elem->token))
				continue;

			/* load token */
			tokens[j].get_token(elem, object,
					    offset + tokens[j].offset,
					    tokens[j].size);

			found++;
		}
	}

	return found;
}

/**
 * sof_parse_token_sets - Parse multiple sets of tokens
 * @scomp: pointer to soc component
 * @object: target ipc struct for parsed values
 * @tokens: token definition array describing what tokens to parse
 * @count: number of tokens in definition array
 * @array: source pointer to consecutive vendor arrays to be parsed
 * @priv_size: total size of the consecutive source arrays
 * @sets: number of similar token sets to be parsed, 1 set has count elements
 * @object_size: offset to next target ipc struct with multiple sets
 *
 * This function parses multiple sets of tokens in vendor arrays into
 * consecutive ipc structs.
 */
int sof_parse_token_sets(struct snd_soc_component *scomp,
				void *object,
				const struct sof_topology_token *tokens,
				int count,
				struct snd_soc_tplg_vendor_array *array,
				int priv_size, int sets, size_t object_size)
{
	size_t offset = 0;
	int found = 0;
	int total = 0;
	int asize;

	while (priv_size > 0 && total < count * sets) {
		asize = le32_to_cpu(array->size);

		/* validate asize */
		if (asize < 0) { /* FIXME: A zero-size array makes no sense */
			dev_err(scomp->dev, "error: invalid array size 0x%x\n",
				asize);
			return -EINVAL;
		}

		/* make sure there is enough data before parsing */
		priv_size -= asize;
		if (priv_size < 0) {
			dev_err(scomp->dev, "error: invalid array size 0x%x\n",
				asize);
			return -EINVAL;
		}

		/* call correct parser depending on type */
		switch (le32_to_cpu(array->type)) {
		case SND_SOC_TPLG_TUPLE_TYPE_UUID:
			found += sof_parse_uuid_tokens(scomp, object, tokens,
						       count, array, offset);
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_STRING:
			found += sof_parse_string_tokens(scomp, object, tokens,
							 count, array, offset);
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_BOOL:
		case SND_SOC_TPLG_TUPLE_TYPE_BYTE:
		case SND_SOC_TPLG_TUPLE_TYPE_WORD:
		case SND_SOC_TPLG_TUPLE_TYPE_SHORT:
			found += sof_parse_word_tokens(scomp, object, tokens,
						       count, array, offset);
			break;
		default:
			dev_err(scomp->dev, "error: unknown token type %d\n",
				array->type);
			return -EINVAL;
		}

		/* next array */
		array = (struct snd_soc_tplg_vendor_array *)((u8 *)array
			+ asize);

		/* move to next target struct */
		if (found >= count) {
			offset += object_size;
			total += found;
			found = 0;
		}
	}

	return 0;
}

int sof_parse_tokens(struct snd_soc_component *scomp,
			    void *object,
			    const struct sof_topology_token *tokens,
			    int count,
			    struct snd_soc_tplg_vendor_array *array,
			    int priv_size)
{
	/*
	 * sof_parse_tokens is used when topology contains only a single set of
	 * identical tuples arrays. So additional parameters to
	 * sof_parse_token_sets are sets = 1 (only 1 set) and
	 * object_size = 0 (irrelevant).
	 */
	return sof_parse_token_sets(scomp, object, tokens, count, array,
				    priv_size, 1, 0);
}

void sof_dai_set_format(struct snd_soc_tplg_hw_config *hw_config,
			       struct sof_ipc_dai_config *config)
{
	/* clock directions wrt codec */
	if (hw_config->bclk_provider == SND_SOC_TPLG_BCLK_CM) {
		/* codec is bclk master */
		if (hw_config->fsync_provider == SND_SOC_TPLG_FSYNC_CM)
			config->format |= SOF_DAI_FMT_CBM_CFM;
		else
			config->format |= SOF_DAI_FMT_CBM_CFS;
	} else {
		/* codec is bclk slave */
		if (hw_config->fsync_provider == SND_SOC_TPLG_FSYNC_CM)
			config->format |= SOF_DAI_FMT_CBS_CFM;
		else
			config->format |= SOF_DAI_FMT_CBS_CFS;
	}

	/* inverted clocks ? */
	if (hw_config->invert_bclk) {
		if (hw_config->invert_fsync)
			config->format |= SOF_DAI_FMT_IB_IF;
		else
			config->format |= SOF_DAI_FMT_IB_NF;
	} else {
		if (hw_config->invert_fsync)
			config->format |= SOF_DAI_FMT_NB_IF;
		else
			config->format |= SOF_DAI_FMT_NB_NF;
	}
}

int sof_connect_dai_widget(struct snd_soc_component *scomp,
				  struct snd_soc_dapm_widget *w,
				  struct snd_soc_tplg_dapm_widget *tw,
				  struct snd_sof_dai *dai)
{
	struct snd_soc_card *card = scomp->card;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *cpu_dai;
	int i;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		dev_vdbg(scomp->dev, "tplg: check widget: %s stream: %s dai stream: %s\n",
			 w->name,  w->sname, rtd->dai_link->stream_name);

		if (!w->sname || !rtd->dai_link->stream_name)
			continue;

		/* does stream match DAI link ? */
		if (strcmp(w->sname, rtd->dai_link->stream_name))
			continue;

		switch (w->id) {
		case snd_soc_dapm_dai_out:
			for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
				/*
				 * Please create DAI widget in the right order
				 * to ensure BE will connect to the right DAI
				 * widget.
				 */
				if (!cpu_dai->capture_widget) {
					cpu_dai->capture_widget = w;
					break;
				}
			}
			if (i == rtd->num_cpus) {
				dev_err(scomp->dev, "error: can't find BE for DAI %s\n",
					w->name);

				return -EINVAL;
			}
			dai->name = rtd->dai_link->name;
			dev_dbg(scomp->dev, "tplg: connected widget %s -> DAI link %s\n",
				w->name, rtd->dai_link->name);
			break;
		case snd_soc_dapm_dai_in:
			for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
				/*
				 * Please create DAI widget in the right order
				 * to ensure BE will connect to the right DAI
				 * widget.
				 */
				if (!cpu_dai->playback_widget) {
					cpu_dai->playback_widget = w;
					break;
				}
			}
			if (i == rtd->num_cpus) {
				dev_err(scomp->dev, "error: can't find BE for DAI %s\n",
					w->name);

				return -EINVAL;
			}
			dai->name = rtd->dai_link->name;
			dev_dbg(scomp->dev, "tplg: connected widget %s -> DAI link %s\n",
				w->name, rtd->dai_link->name);
			break;
		default:
			break;
		}
	}

	/* check we have a connection */
	if (!dai->name) {
		dev_err(scomp->dev, "error: can't connect DAI %s stream %s\n",
			w->name, w->sname);
		return -EINVAL;
	}

	return 0;
}

/* bind PCM ID to host component ID */
int spcm_bind(struct snd_soc_component *scomp, struct snd_sof_pcm *spcm, int dir)
{
	struct snd_sof_widget *host_widget;

	host_widget = snd_sof_find_swidget_sname(scomp,
						 spcm->pcm.caps[dir].name,
						 dir);
	if (!host_widget) {
		dev_err(scomp->dev, "can't find host comp %s to bind pcm %s\n",
			spcm->pcm.caps[dir].name, spcm->pcm.pcm_name);
		return -EINVAL;
	}

	spcm->stream[dir].comp_id = host_widget->comp_id;

	return 0;
}

/* set pipe_widget for comp */
int sof_set_comp_pipe_widget(struct snd_sof_dev *sdev, struct snd_sof_widget *pipe_widget,
				    struct snd_sof_widget *comp_swidget)
{
	struct snd_sof_control *scontrol;

	/* set the pipe_widget and apply the dynamic_pipeline_widget_flag */
	comp_swidget->pipe_widget = pipe_widget;
	comp_swidget->dynamic_pipeline_widget = pipe_widget->dynamic_pipeline_widget;

	if (!comp_swidget->dynamic_pipeline_widget)
		return 0;

	/* dynamic widgets cannot have volatile kcontrols */
	list_for_each_entry(scontrol, &sdev->kcontrol_list, list)
		if (scontrol->comp_id == comp_swidget->comp_id) {
			if (scontrol->access & SNDRV_CTL_ELEM_ACCESS_VOLATILE) {
				dev_err(sdev->dev, "error: volatile control found for dynamic widget %s\n",
					comp_swidget->widget->sname);
				return -EINVAL;
			}
		}

	return 0;
}

/*
 * Send IPC and set the same config for all DAIs with name matching the link
 * name. Note that the function can only be used for the case that all DAIs
 * have a common DAI config for now.
 */
static int sof_set_dai_config_multi(struct snd_sof_dev *sdev, u32 size,
				    struct snd_soc_dai_link *link,
				    struct sof_ipc_dai_config *config,
				    int num_conf, int curr_conf)
{
	struct snd_sof_dai *dai;
	int found = 0;
	int i;

	list_for_each_entry(dai, &sdev->dai_list, list) {
		if (!dai->name)
			continue;

		if (strcmp(link->name, dai->name) == 0) {
			/*
			 * the same dai config will be applied to all DAIs in
			 * the same dai link. We have to ensure that the ipc
			 * dai config's dai_index match to the component's
			 * dai_index.
			 */
			for (i = 0; i < num_conf; i++)
				config[i].dai_index = dai->comp_dai.dai_index;

			dev_dbg(sdev->dev, "set DAI config for %s index %d\n",
				dai->name, config[curr_conf].dai_index);

			dai->number_configs = num_conf;
			dai->current_config = curr_conf;
			dai->dai_config = kmemdup(config, size * num_conf, GFP_KERNEL);
			if (!dai->dai_config)
				return -ENOMEM;

			found = 1;
		}
	}

	/*
	 * machine driver may define a dai link with playback and capture
	 * dai enabled, but the dai link in topology would support both, one
	 * or none of them. Here print a warning message to notify user
	 */
	if (!found) {
		dev_warn(sdev->dev, "warning: failed to find dai for dai link %s",
			 link->name);
	}

	return 0;
}

int sof_set_dai_config(struct snd_sof_dev *sdev, u32 size,
		       struct snd_soc_dai_link *link,
		       struct sof_ipc_dai_config *config)
{
	return sof_set_dai_config_multi(sdev, size, link, config, 1, 0);
}


int sof_link_ssp_load(struct snd_soc_component *scomp, int index,
			     struct snd_soc_dai_link *link,
			     struct snd_soc_tplg_link_config *cfg,
			     struct snd_soc_tplg_hw_config *hw_config,
			     struct sof_ipc_dai_config *config, int curr_conf)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &cfg->priv;
	int num_conf = le32_to_cpu(cfg->num_hw_configs);
	u32 size = sizeof(*config);
	int ret;
	int i;

	/*
	 * Parse common data, we should have 1 common data per hw_config.
	 */
	ret = sof_parse_token_sets(scomp, &config->ssp, ssp_tokens,
				   ARRAY_SIZE(ssp_tokens), private->array,
				   le32_to_cpu(private->size),
				   num_conf, size);

	if (ret != 0) {
		dev_err(scomp->dev, "error: parse ssp tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	/* process all possible hw configs */
	for (i = 0; i < num_conf; i++) {

		/* handle master/slave and inverted clocks */
		sof_dai_set_format(&hw_config[i], &config[i]);

		config[i].hdr.size = size;

		/* copy differentiating hw configs to ipc structs */
		config[i].ssp.mclk_rate = le32_to_cpu(hw_config[i].mclk_rate);
		config[i].ssp.bclk_rate = le32_to_cpu(hw_config[i].bclk_rate);
		config[i].ssp.fsync_rate = le32_to_cpu(hw_config[i].fsync_rate);
		config[i].ssp.tdm_slots = le32_to_cpu(hw_config[i].tdm_slots);
		config[i].ssp.tdm_slot_width = le32_to_cpu(hw_config[i].tdm_slot_width);
		config[i].ssp.mclk_direction = hw_config[i].mclk_direction;
		config[i].ssp.rx_slots = le32_to_cpu(hw_config[i].rx_slots);
		config[i].ssp.tx_slots = le32_to_cpu(hw_config[i].tx_slots);

		dev_dbg(scomp->dev, "tplg: config SSP%d fmt %#x mclk %d bclk %d fclk %d width (%d)%d slots %d mclk id %d quirks %d clks_control %#x\n",
			config[i].dai_index, config[i].format,
			config[i].ssp.mclk_rate, config[i].ssp.bclk_rate,
			config[i].ssp.fsync_rate, config[i].ssp.sample_valid_bits,
			config[i].ssp.tdm_slot_width, config[i].ssp.tdm_slots,
			config[i].ssp.mclk_id, config[i].ssp.quirks, config[i].ssp.clks_control);

		/* validate SSP fsync rate and channel count */
		if (config[i].ssp.fsync_rate < 8000 || config[i].ssp.fsync_rate > 192000) {
			dev_err(scomp->dev, "error: invalid fsync rate for SSP%d\n",
				config[i].dai_index);
			return -EINVAL;
		}

		if (config[i].ssp.tdm_slots < 1 || config[i].ssp.tdm_slots > 8) {
			dev_err(scomp->dev, "error: invalid channel count for SSP%d\n",
				config[i].dai_index);
			return -EINVAL;
		}
	}

	/* set config for all DAI's with name matching the link name */
	ret = sof_set_dai_config_multi(sdev, size, link, config, num_conf, curr_conf);
	if (ret < 0)
		dev_err(scomp->dev, "error: failed to save DAI config for SSP%d\n",
			config->dai_index);

	return ret;
}

int sof_link_dmic_load(struct snd_soc_component *scomp, int index,
			      struct snd_soc_dai_link *link,
			      struct snd_soc_tplg_link_config *cfg,
			      struct snd_soc_tplg_hw_config *hw_config,
			      struct sof_ipc_dai_config *config)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct sof_ipc_fw_ready *ready = &sdev->fw_ready;
	struct sof_ipc_fw_version *v = &ready->version;
	size_t size = sizeof(*config);
	int ret, j;

	/* Ensure the entire DMIC config struct is zeros */
	memset(&config->dmic, 0, sizeof(struct sof_ipc_dai_dmic_params));

	/* get DMIC tokens */
	ret = sof_parse_tokens(scomp, &config->dmic, dmic_tokens,
			       ARRAY_SIZE(dmic_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(scomp->dev, "error: parse dmic tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	/* get DMIC PDM tokens */
	ret = sof_parse_token_sets(scomp, &config->dmic.pdm[0], dmic_pdm_tokens,
			       ARRAY_SIZE(dmic_pdm_tokens), private->array,
			       le32_to_cpu(private->size),
			       config->dmic.num_pdm_active,
			       sizeof(struct sof_ipc_dai_dmic_pdm_ctrl));

	if (ret != 0) {
		dev_err(scomp->dev, "error: parse dmic pdm tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	/* set IPC header size */
	config->hdr.size = size;

	/* debug messages */
	dev_dbg(scomp->dev, "tplg: config DMIC%d driver version %d\n",
		config->dai_index, config->dmic.driver_ipc_version);
	dev_dbg(scomp->dev, "pdmclk_min %d pdm_clkmax %d duty_min %d\n",
		config->dmic.pdmclk_min, config->dmic.pdmclk_max,
		config->dmic.duty_min);
	dev_dbg(scomp->dev, "duty_max %d fifo_fs %d num_pdms active %d\n",
		config->dmic.duty_max, config->dmic.fifo_fs,
		config->dmic.num_pdm_active);
	dev_dbg(scomp->dev, "fifo word length %d\n", config->dmic.fifo_bits);

	for (j = 0; j < config->dmic.num_pdm_active; j++) {
		dev_dbg(scomp->dev, "pdm %d mic a %d mic b %d\n",
			config->dmic.pdm[j].id,
			config->dmic.pdm[j].enable_mic_a,
			config->dmic.pdm[j].enable_mic_b);
		dev_dbg(scomp->dev, "pdm %d polarity a %d polarity b %d\n",
			config->dmic.pdm[j].id,
			config->dmic.pdm[j].polarity_mic_a,
			config->dmic.pdm[j].polarity_mic_b);
		dev_dbg(scomp->dev, "pdm %d clk_edge %d skew %d\n",
			config->dmic.pdm[j].id,
			config->dmic.pdm[j].clk_edge,
			config->dmic.pdm[j].skew);
	}

	/*
	 * this takes care of backwards compatible handling of fifo_bits_b.
	 * It is deprecated since firmware ABI version 3.0.1.
	 */
	if (SOF_ABI_VER(v->major, v->minor, v->micro) < SOF_ABI_VER(3, 0, 1))
		config->dmic.fifo_bits_b = config->dmic.fifo_bits;

	/* set config for all DAI's with name matching the link name */
	ret = sof_set_dai_config(sdev, size, link, config);
	if (ret < 0)
		dev_err(scomp->dev, "error: failed to save DAI config for DMIC%d\n",
			config->dai_index);

	return ret;
}

int sof_link_hda_load(struct snd_soc_component *scomp, int index,
			     struct snd_soc_dai_link *link,
			     struct snd_soc_tplg_link_config *cfg,
			     struct snd_soc_tplg_hw_config *hw_config,
			     struct sof_ipc_dai_config *config)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct snd_soc_dai *dai;
	u32 size = sizeof(*config);
	int ret;

	/* init IPC */
	memset(&config->hda, 0, sizeof(struct sof_ipc_dai_hda_params));
	config->hdr.size = size;

	/* get any bespoke DAI tokens */
	ret = sof_parse_tokens(scomp, &config->hda, hda_tokens,
			       ARRAY_SIZE(hda_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(scomp->dev, "error: parse hda tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	dev_dbg(scomp->dev, "HDA config rate %d channels %d\n",
		config->hda.rate, config->hda.channels);

	dai = snd_soc_find_dai(link->cpus);
	if (!dai) {
		dev_err(scomp->dev, "error: failed to find dai %s in %s",
			link->cpus->dai_name, __func__);
		return -EINVAL;
	}

	config->hda.link_dma_ch = DMA_CHAN_INVALID;

	ret = sof_set_dai_config(sdev, size, link, config);
	if (ret < 0)
		dev_err(scomp->dev, "error: failed to process hda dai link %s",
			link->name);

	return ret;
}

