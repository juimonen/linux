// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
//

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>
#include <sound/pcm_params.h>
#include <sound/sof/topology.h>
#include <uapi/sound/sof/tokens.h>
#include "ops.h"
#include "sof-audio.h"
#include "sof-priv.h"
#include "ipc4.h"
#include "ipc4-topology.h"
#include "topology-common.h"

static const struct sof_topology_token ipc4_sched_tokens[] = {
	{SOF_TKN_SCHED_LP_MOD, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct sof_ipc4_pipeline, lp_mode), 0}
};

/* Generic components */
static const struct sof_topology_token ipc4_comp_tokens[] = {
	{SOF_TKN_COMP_CPC, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct basic_module_cfg, cpc), 0},
};

/**
 * sof_ipc4_comp_alloc - allocate and initialize buffer for a new component
 * @swidget: pointer to struct snd_sof_widget containing extended data
 * @ipc_size: IPC payload size that will be updated depending on valid
 *  extended data.
 * @index: ID of the pipeline the component belongs to
 *
 * Return: The pointer to the new allocated component, NULL if failed.
 */
static struct sof_ipc_comp *sof_ipc4_comp_alloc(struct snd_sof_widget *swidget,
					   size_t *ipc_size, int index)
{
	u8 nil_uuid[SOF_UUID_SIZE] = {0};
	struct sof_ipc_comp *comp;
	size_t total_size = *ipc_size;

	/* only non-zero UUID is valid */
	if (memcmp(&swidget->comp_ext, nil_uuid, SOF_UUID_SIZE))
		total_size += sizeof(swidget->comp_ext);

	comp = kzalloc(total_size, GFP_KERNEL);
	if (!comp)
		return NULL;

	comp->id = swidget->comp_id;
	comp->pipeline_id = index;
	comp->core = swidget->core;

	/* handle the extended data if needed */
	if (total_size > *ipc_size) {
		/* append extended data to the end of the component */
		memcpy((u8 *)comp + *ipc_size, &swidget->comp_ext, sizeof(swidget->comp_ext));
		comp->ext_data_length = sizeof(swidget->comp_ext);
	}

	/* update ipc_size and return */
	*ipc_size = total_size;
	return comp;
}

static int sof_ipc4_widget_load_dai(struct snd_soc_component *scomp, int index,
			       struct snd_sof_widget *swidget,
			       struct snd_soc_tplg_dapm_widget *tw,
			       struct sof_ipc4_dai *ipc4_dai)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc_comp_dai *comp_dai;
	size_t size = sizeof(*comp_dai);
	int ret;

	comp_dai = (struct sof_ipc_comp_dai *)
		   sof_ipc4_comp_alloc(swidget, &size, index);
	if (!comp_dai)
		return -ENOMEM;

	ret = sof_parse_topology_tokens(scomp, comp_dai, SOF_TOKEN_DAI,
					private->array, private->size);
	if (ret != 0)
		goto finish;

	ret = sof_parse_tokens(scomp, &ipc4_dai->copier.base_config, ipc4_comp_tokens,
			       ARRAY_SIZE(ipc4_comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(scomp->dev, "error: parse dai.cfg tokens failed %d\n",
			private->size);
		goto finish;
	}

	dev_dbg(scomp->dev, "dai %s cpc %d, type %d\n", swidget->widget->name,
			ipc4_dai->copier.base_config.cpc, comp_dai->type);

	if (ipc4_dai) {
		ipc4_dai->dai.scomp = scomp;

		/*
		 * copy only the sof_ipc_comp_dai to avoid collapsing
		 * the snd_sof_dai, the extended data is kept in the
		 * snd_sof_widget.
		 */
		memcpy(&ipc4_dai->dai.comp_dai, comp_dai, sizeof(*comp_dai));
	}

finish:
	kfree(comp_dai);
	return ret;
}

/*
 * PCM Topology
 */

static int sof_ipc4_widget_load_pcm(struct snd_soc_component *scomp, int index,
			       struct snd_sof_widget *swidget,
			       enum sof_ipc_stream_direction dir,
			       struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc4_host *host;
	size_t size = sizeof(*host);
	int ret;

	host = (struct sof_ipc4_host *)
	       sof_ipc4_comp_alloc(swidget, &size, index);
	if (!host)
		return -ENOMEM;

	ret = sof_parse_tokens(scomp, &host->copier.base_config, ipc4_comp_tokens,
			       ARRAY_SIZE(ipc4_comp_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(scomp->dev, "error: parse host.cfg tokens failed %d\n",
			le32_to_cpu(private->size));
		goto err;
	}

	dev_dbg(scomp->dev, "loaded host %s cpc %d\n", swidget->widget->name,
				host->copier.base_config.cpc);

	swidget->private = host;

	return 0;
err:
	kfree(host);
	return ret;
}

static int sof_ipc4_widget_load_pipeline(struct snd_soc_component *scomp, int index,
				    struct snd_sof_widget *swidget,
				    struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	struct sof_ipc4_pipeline *pipeline;
	int ret;

	pipeline = kzalloc(sizeof(*pipeline), GFP_KERNEL);
	if (!pipeline)
		return -ENOMEM;

	pipeline->pipe_new.pipeline_id = swidget->pipeline_id;

	ret = sof_parse_topology_tokens(scomp, pipeline, SOF_TOKEN_SCHED,
					private->array, private->size);
	if (ret != 0)
		goto err;

	ret = sof_parse_tokens(scomp, pipeline, ipc4_sched_tokens,
			       ARRAY_SIZE(ipc4_sched_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret != 0) {
		dev_err(scomp->dev, "error: parse pipeline tokens failed %d\n",
			private->size);
		goto err;
	}

	dev_dbg(scomp->dev, "pipeline %s: id %d pri %d core %d lp mode %d\n",
		swidget->widget->name, pipeline->pipe_new.pipeline_id,
		pipeline->pipe_new.priority, pipeline->pipe_new.core,
		pipeline->lp_mode);

	swidget->private = pipeline;

	return 0;
err:
	kfree(pipeline);
	return ret;
}

/* external widget init - used for any driver specific init */
static int sof_ipc4_widget_ready(struct snd_soc_component *scomp, int index,
			    struct snd_soc_dapm_widget *w,
			    struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *swidget;
	struct sof_ipc_comp comp = {
		.core = SOF_DSP_PRIMARY_CORE,
	};
	int ret = 0;

	swidget = kzalloc(sizeof(*swidget), GFP_KERNEL);
	if (!swidget)
		return -ENOMEM;

	swidget->scomp = scomp;
	swidget->widget = w;
	swidget->complete = 0;
	swidget->id = w->id;
	swidget->comp_id = sdev->next_comp_id++;
	swidget->pipeline_id = index;
	swidget->private = NULL;

	dev_dbg(scomp->dev, "tplg2: ready widget pipe %d  comp %d type %d name : %s stream %s\n",
		index, swidget->comp_id, swidget->id, tw->name,
		strnlen(tw->sname, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) > 0
			? tw->sname : "none");

	swidget->core = comp.core;

	ret = sof_parse_topology_tokens(scomp, &swidget->comp_ext, SOF_TOKEN_EXT,
					tw->priv.array, tw->priv.size);
	if (ret != 0) {
		kfree(swidget);
		return ret;
	}

	/* handle any special case widgets */
	switch (w->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct sof_ipc4_dai *ipc4_dai;

		ipc4_dai = kzalloc(sizeof(*ipc4_dai), GFP_KERNEL);
		if (!ipc4_dai) {
			kfree(swidget);
			return -ENOMEM;
		}

		ret = sof_ipc4_widget_load_dai(scomp, index, swidget, tw, ipc4_dai);
		if (ret == 0) {
			sof_connect_dai_widget(scomp, w, tw, &ipc4_dai->dai);
			list_add(&ipc4_dai->dai.list, &sdev->dai_list);
			swidget->private = ipc4_dai;
		} else {
			kfree(ipc4_dai);
		}
		break;
	}
	case snd_soc_dapm_scheduler:
		ret = sof_ipc4_widget_load_pipeline(scomp, index, swidget, tw);
		break;
	case snd_soc_dapm_aif_out:
		ret = sof_ipc4_widget_load_pcm(scomp, index, swidget,
					  SOF_IPC_STREAM_CAPTURE, tw);
		break;
	case snd_soc_dapm_aif_in:
		ret = sof_ipc4_widget_load_pcm(scomp, index, swidget,
					  SOF_IPC_STREAM_PLAYBACK, tw);
		break;
	case snd_soc_dapm_switch:
	case snd_soc_dapm_dai_link:
	case snd_soc_dapm_kcontrol:
	default:
		dev_dbg(scomp->dev, "widget type %d name %s not handled\n", swidget->id, tw->name);
		break;
	}

	if (ret < 0) {
		dev_err(scomp->dev,
			"error: DSP failed to add widget id %d type %d name : %s stream %s\n",
			tw->shift, swidget->id, tw->name,
			strnlen(tw->sname, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) > 0
				? tw->sname : "none");
		kfree(swidget);
		return ret;
	}

	w->dobj.private = swidget;
	list_add(&swidget->list, &sdev->widget_list);

	return ret;
}

static int sof_ipc4_widget_unload(struct snd_soc_component *scomp,
			     struct snd_soc_dobj *dobj)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct snd_kcontrol_new *kc;
	struct snd_soc_dapm_widget *widget;
	struct sof_ipc_pipe_new *pipeline;
	struct snd_sof_control *scontrol;
	struct snd_sof_widget *swidget;
	struct soc_mixer_control *sm;
	struct soc_bytes_ext *sbe;
	struct soc_enum *se;
	int ret = 0;
	int i;

	swidget = dobj->private;
	if (!swidget)
		return 0;

	widget = swidget->widget;

	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct sof_ipc4_dai *ipc4_dai;

		ipc4_dai = swidget->private;

		if (ipc4_dai) {
			/* free dai config */
			kfree(ipc4_dai->dai.dai_config);
			list_del(&ipc4_dai->dai.list);
		}
		break;
	}
	case snd_soc_dapm_scheduler:

		/* power down the pipeline schedule core */
		pipeline = swidget->private;
		ret = snd_sof_dsp_core_power_down(sdev, 1 << pipeline->core);
		if (ret < 0)
			dev_err(scomp->dev, "error: powering down pipeline schedule core %d\n",
				pipeline->core);

		/* update enabled cores mask */
		sdev->enabled_cores_mask &= ~(1 << pipeline->core);

		break;
	default:
		break;
	}
	for (i = 0; i < widget->num_kcontrols; i++) {
		kc = &widget->kcontrol_news[i];
		switch (widget->dobj.widget.kcontrol_type[i]) {
		case SND_SOC_TPLG_TYPE_MIXER:
			sm = (struct soc_mixer_control *)kc->private_value;
			scontrol = sm->dobj.private;
			if (sm->max > 1)
				kfree(scontrol->volume_table);
			break;
		case SND_SOC_TPLG_TYPE_ENUM:
			se = (struct soc_enum *)kc->private_value;
			scontrol = se->dobj.private;
			break;
		case SND_SOC_TPLG_TYPE_BYTES:
			sbe = (struct soc_bytes_ext *)kc->private_value;
			scontrol = sbe->dobj.private;
			break;
		default:
			dev_warn(scomp->dev, "unsupported kcontrol_type\n");
			goto out;
		}
		kfree(scontrol->control_data);
		list_del(&scontrol->list);
		kfree(scontrol);
	}

out:
	/* free private value */
	kfree(swidget->private);

	/* remove and free swidget object */
	list_del(&swidget->list);
	kfree(swidget);

	return ret;
}

/*
 * DAI HW configuration.
 */

/* FE DAI - used for any driver specific init */
static int sof_ipc4_dai_load(struct snd_soc_component *scomp, int index,
			struct snd_soc_dai_driver *dai_drv,
			struct snd_soc_tplg_pcm *pcm, struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_stream_caps *caps;
	struct snd_sof_pcm *spcm;
	int stream;
	int ret;

	/* nothing to do for BEs atm */
	if (!pcm)
		return 0;

	spcm = kzalloc(sizeof(*spcm), GFP_KERNEL);
	if (!spcm)
		return -ENOMEM;

	spcm->scomp = scomp;

	for_each_pcm_streams(stream) {
		spcm->stream[stream].comp_id = COMP_ID_UNASSIGNED;
		INIT_WORK(&spcm->stream[stream].period_elapsed_work,
			  snd_sof_pcm_period_elapsed_work);
	}

	spcm->pcm = *pcm;
	dev_dbg(scomp->dev, "tplg2: load pcm %s\n", pcm->dai_name);

	dai_drv->dobj.private = spcm;
	list_add(&spcm->list, &sdev->pcm_list);

	/* do we need to allocate playback PCM DMA pages */
	if (!spcm->pcm.playback)
		goto capture;

	stream = SNDRV_PCM_STREAM_PLAYBACK;

	dev_vdbg(scomp->dev, "tplg2: pcm %s stream tokens: playback d0i3:%d\n",
		 spcm->pcm.pcm_name, spcm->stream[stream].d0i3_compatible);

	caps = &spcm->pcm.caps[stream];

	/* allocate playback page table buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev,
				  PAGE_SIZE, &spcm->stream[stream].page_table);
	if (ret < 0) {
		dev_err(scomp->dev, "error: can't alloc page table for %s %d\n",
			caps->name, ret);

		return ret;
	}

	/* bind pcm to host comp */
	ret = spcm_bind(scomp, spcm, stream);
	if (ret) {
		dev_err(scomp->dev,
			"error: can't bind pcm to host\n");
		goto free_playback_tables;
	}

capture:
	stream = SNDRV_PCM_STREAM_CAPTURE;

	/* do we need to allocate capture PCM DMA pages */
	if (!spcm->pcm.capture)
		return ret;

	dev_vdbg(scomp->dev, "tplg2: pcm %s stream tokens: capture d0i3:%d\n",
		 spcm->pcm.pcm_name, spcm->stream[stream].d0i3_compatible);

	caps = &spcm->pcm.caps[stream];

	/* allocate capture page table buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev,
				  PAGE_SIZE, &spcm->stream[stream].page_table);
	if (ret < 0) {
		dev_err(scomp->dev, "error: can't alloc page table for %s %d\n",
			caps->name, ret);
		goto free_playback_tables;
	}

	/* bind pcm to host comp */
	ret = spcm_bind(scomp, spcm, stream);
	if (ret) {
		dev_err(scomp->dev,
			"error: can't bind pcm to host\n");
		snd_dma_free_pages(&spcm->stream[stream].page_table);
		goto free_playback_tables;
	}

	return ret;

free_playback_tables:
	if (spcm->pcm.playback)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].page_table);

	return ret;
}

static int sof_ipc4_dai_unload(struct snd_soc_component *scomp,
			  struct snd_soc_dobj *dobj)
{
	struct snd_sof_pcm *spcm = dobj->private;

	/* free PCM DMA pages */
	if (spcm->pcm.playback)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].page_table);

	if (spcm->pcm.capture)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_CAPTURE].page_table);

	/* remove from list and free spcm */
	list_del(&spcm->list);
	kfree(spcm);

	return 0;
}

static int sof_ipc4_set_dai_config(struct snd_sof_dev *sdev, u32 size,
			      struct snd_soc_dai_link *link,
			      struct sof_ipc_dai_config *config)
{
	struct snd_sof_dai *dai;
	int found = 0;

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
			config->dai_index = dai->comp_dai.dai_index;

			dai->dai_config = kmemdup(config, size, GFP_KERNEL);
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

/* DAI link - used for any driver specific init */
static int sof_ipc4_link_load(struct snd_soc_component *scomp, int index,
			 struct snd_soc_dai_link *link,
			 struct snd_soc_tplg_link_config *cfg)
{
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct snd_soc_tplg_hw_config *hw_config;
	struct sof_ipc_dai_config config;
	struct snd_sof_dev *sdev;
	int num_hw_configs;
	int ret;
	int i = 0;

	if (!link->platforms) {
		dev_err(scomp->dev, "error: no platforms\n");
		return -EINVAL;
	}
	link->platforms->name = dev_name(scomp->dev);

	/* set nonatomic property for FE dai links */
	if (!link->no_pcm) {
		link->nonatomic = true;
		return 0;
	}

	/* check we have some tokens - we need at least DAI type */
	if (le32_to_cpu(private->size) == 0) {
		dev_err(scomp->dev, "error: expected tokens for DAI, none found\n");
		return -EINVAL;
	}

	memset(&config, 0, sizeof(config));
	/* get any common DAI tokens */
	ret = sof_parse_topology_tokens(scomp, &config, SOF_TOKEN_DAI_LINK,
					private->array, private->size);
	if (ret != 0)
		return ret;

	/*
	 * DAI links are expected to have at least 1 hw_config.
	 * But some older topologies might have no hw_config for HDA dai links.
	 */
	num_hw_configs = le32_to_cpu(cfg->num_hw_configs);
	if (!num_hw_configs) {
		if (config.type != SOF_DAI_INTEL_HDA) {
			dev_err(scomp->dev, "error: unexpected DAI config count %d!\n",
				le32_to_cpu(cfg->num_hw_configs));
			return -EINVAL;
		}
	} else {
		dev_dbg(scomp->dev, "tplg2: %d hw_configs found, default id: %d!\n",
			cfg->num_hw_configs, le32_to_cpu(cfg->default_hw_config_id));

		for (i = 0; i < num_hw_configs; i++) {
			if (cfg->hw_config[i].id == cfg->default_hw_config_id)
				break;
		}

		if (i == num_hw_configs) {
			dev_err(scomp->dev, "error: default hw_config id: %d not found!\n",
				le32_to_cpu(cfg->default_hw_config_id));
			return -EINVAL;
		}
	}

	hw_config = &cfg->hw_config[i];
	config.format = le32_to_cpu(hw_config->fmt);

	switch (config.type) {
	case SOF_DAI_INTEL_SSP:
		ret = sof_link_ssp_load(scomp, index, link, cfg, hw_config, &config, 0);
		break;
	default:
		dev_err(scomp->dev, "error: invalid DAI type %d\n",
			config.type);
		ret = -EINVAL;
		break;
	}
	if (ret < 0)
		return ret;

	sdev = snd_soc_component_get_drvdata(scomp);
	/* set config for all DAI's with name matching the link name */
	ret = sof_ipc4_set_dai_config(sdev, sizeof(config), link, &config);
	if (ret < 0)
		dev_err(scomp->dev, "error: failed to save DAI config for link %d index %d\n",
			config.type, config.dai_index);

	return ret;
}

static int sof_ipc4_link_unload(struct snd_soc_component *scomp,
			   struct snd_soc_dobj *dobj)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_dai_link *link =
		container_of(dobj, struct snd_soc_dai_link, dobj);

	struct snd_sof_dai *sof_dai;
	int ret = 0;

	/* only BE link is loaded by sof */
	if (!link->no_pcm)
		return 0;

	list_for_each_entry(sof_dai, &sdev->dai_list, list) {
		if (!sof_dai->name)
			continue;

		if (strcmp(link->name, sof_dai->name) == 0)
			goto found;
	}

	dev_err(scomp->dev, "error: failed to find dai %s in %s",
		link->name, __func__);
	return -EINVAL;
found:

	switch (sof_dai->dai_config->type) {
	case SOF_DAI_INTEL_SSP:
		/* no resource needs to be released for all cases above */
		break;
	default:
		dev_err(scomp->dev, "error: invalid DAI type %d\n",
			sof_dai->dai_config->type);
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* DAI link - used for any driver specific init */
static int sof_ipc4_route_load(struct snd_soc_component *scomp, int index,
			  struct snd_soc_dapm_route *route)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *source_swidget, *sink_swidget;
	struct sof_ipc_pipe_comp_connect *connect;
	struct snd_soc_dobj *dobj = &route->dobj;
	struct snd_sof_route *sroute;
	int ret = 0;

	/* allocate memory for sroute and connect */
	sroute = kzalloc(sizeof(*sroute), GFP_KERNEL);
	if (!sroute)
		return -ENOMEM;

	sroute->scomp = scomp;

	connect = kzalloc(sizeof(*connect), GFP_KERNEL);
	if (!connect) {
		kfree(sroute);
		return -ENOMEM;
	}

	dev_dbg(scomp->dev, "sink %s control %s source %s\n",
		route->sink, route->control ? route->control : "none",
		route->source);

	/* source component */
	source_swidget = snd_sof_find_swidget(scomp, (char *)route->source);
	if (!source_swidget) {
		dev_err(scomp->dev, "error: source %s not found\n",
			route->source);
		ret = -EINVAL;
		goto err;
	}

	connect->source_id = source_swidget->comp_id;

	/* sink component */
	sink_swidget = snd_sof_find_swidget(scomp, (char *)route->sink);
	if (!sink_swidget) {
		dev_err(scomp->dev, "error: sink %s not found\n",
			route->sink);
		ret = -EINVAL;
		goto err;
	}

	/*
	 * Don't send routes whose sink widget is of type
	 * output or out_drv to the DSP
	 */
	if (sink_swidget->id == snd_soc_dapm_out_drv ||
	    sink_swidget->id == snd_soc_dapm_output)
		goto err;

	connect->sink_id = sink_swidget->comp_id;

	sroute->route = route;
	dobj->private = sroute;
	sroute->private = connect;
	sroute->src_widget = source_swidget;
	sroute->sink_widget = sink_swidget;

	/* add route to route list */
	list_add(&sroute->list, &sdev->route_list);

	return 0;
err:
	kfree(connect);
	kfree(sroute);
	return ret;
}

static int sof_ipc4_route_unload(struct snd_soc_component *scomp,
			    struct snd_soc_dobj *dobj)
{
	struct snd_sof_route *sroute;

	sroute = dobj->private;
	if (!sroute)
		return 0;

	/* free sroute and its private data */
	kfree(sroute->private);
	list_del(&sroute->list);
	kfree(sroute);

	return 0;
}

/* completion - called at completion of firmware loading */
static int sof_ipc4_complete(struct snd_soc_component *scomp)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *swidget, *comp_swidget;
	int ret;

	/* set pipe_widget for all widgets with same pipeline_id */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		switch (swidget->id) {
		case snd_soc_dapm_scheduler:
			list_for_each_entry_reverse(comp_swidget, &sdev->widget_list, list)
				if (comp_swidget->pipeline_id == swidget->pipeline_id) {
					ret = sof_set_comp_pipe_widget(sdev, swidget,
								       comp_swidget);
					if (ret < 0)
						return ret;
				}
			break;
		default:
			break;
		}
	}

	return 0;
}

/* manifest - optional to inform component of manifest */
static int sof_ipc4_manifest(struct snd_soc_component *scomp, int index,
			struct snd_soc_tplg_manifest *man)
{
	u32 size;

	size = le32_to_cpu(man->priv.size);

	/* backward compatible with tplg without ABI info */
	if (!size) {
		dev_dbg(scomp->dev, "No topology ABI info\n");
		return 0;
	}

	dev_info(scomp->dev,
		 "Topology: ABI %d:%d:%d Kernel ABI %d:%d:%d\n",
		 man->priv.data[0], man->priv.data[1],
		 man->priv.data[2], SOF_ABI_MAJOR, SOF_ABI_MINOR,
		 SOF_ABI_PATCH);

	return 0;
}

static struct snd_soc_tplg_ops sof_ipc4_ops = {
	/* external kcontrol init - used for any driver specific init */
	.dapm_route_load	= sof_ipc4_route_load,
	.dapm_route_unload	= sof_ipc4_route_unload,

	/* external widget init - used for any driver specific init */
	/* .widget_load is not currently used */
	.widget_ready	= sof_ipc4_widget_ready,
	.widget_unload	= sof_ipc4_widget_unload,

	/* FE DAI - used for any driver specific init */
	.dai_load	= sof_ipc4_dai_load,
	.dai_unload	= sof_ipc4_dai_unload,

	/* DAI link - used for any driver specific init */
	.link_load	= sof_ipc4_link_load,
	.link_unload	= sof_ipc4_link_unload,

	/* completion - called at completion of firmware loading */
	.complete	= sof_ipc4_complete,

	/* manifest - optional to inform component of manifest */
	.manifest	= sof_ipc4_manifest,
};

int snd_sof_load_topology2(struct snd_soc_component *scomp, const char *file)
{
	const struct firmware *fw;
	int ret;

	dev_dbg(scomp->dev, "loading topology2:%s\n", file);

	ret = request_firmware(&fw, file, scomp->dev);
	if (ret < 0) {
		dev_err(scomp->dev, "error: tplg request firmware %s failed err: %d\n",
			file, ret);
		return ret;
	}

	ret = snd_soc_tplg_component_load(scomp, &sof_ipc4_ops, fw);
	if (ret < 0) {
		dev_err(scomp->dev, "error: tplg2 component load failed %d\n",
			ret);
		ret = -EINVAL;
	}

	release_firmware(fw);
	return ret;
}
EXPORT_SYMBOL(snd_sof_load_topology2);
