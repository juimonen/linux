// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
// Author: Rander Wang <rander.wang@linux.intel.com>
//
// ipc4 intel hardware configuration
//
#include <sound/sof/header.h>
#include "../sof-audio.h"
#include "../ipc4-topology.h"
#include "../ipc4-modules.h"
#include "../sof-priv.h"
#include "../ipc4.h"
#include "../ops.h"
#include "ipc4-intel.h"
#include "hda-ipc.h"
#include "hda.h"
#include "ipc4-i2s.h"
#include "ipc4-alh.h"

static void ipc4_cavs_host_done(struct snd_sof_dev *sdev)
{
	/*
	 * clear busy interrupt to tell dsp controller this
	 * interrupt has been accepted, not trigger it again
	 */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
				       CNL_DSP_REG_HIPCTDR,
				       CNL_DSP_REG_HIPCTDR_BUSY,
				       CNL_DSP_REG_HIPCTDR_BUSY);
	/*
	 * set done bit to ack dsp the msg has been
	 * processed and send reply msg to dsp
	 */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
				       CNL_DSP_REG_HIPCTDA,
				       CNL_DSP_REG_HIPCTDA_DONE,
				       CNL_DSP_REG_HIPCTDA_DONE);
}

static void ipc4_cavs_dsp_done(struct snd_sof_dev *sdev)
{
	/*
	 * set DONE bit - tell DSP we have received the reply msg
	 * from DSP, and processed it, don't send more reply to host
	 */
	snd_sof_dsp_update_bits_forced(sdev, HDA_DSP_BAR,
				       CNL_DSP_REG_HIPCIDA,
				       CNL_DSP_REG_HIPCIDA_DONE,
				       CNL_DSP_REG_HIPCIDA_DONE);

	/* unmask Done interrupt */
	snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR,
				CNL_DSP_REG_HIPCCTL,
				CNL_DSP_REG_HIPCCTL_DONE,
				CNL_DSP_REG_HIPCCTL_DONE);
}

irqreturn_t sof_ipc4_cavs_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = context;
	u32 hipci;
	u32 hipcida;
	u32 hipctdr;
	u32 hipctdd;
	u32 msg;
	u32 msg_ext;
	bool ipc_irq = false;

	hipcida = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDA);
	hipctdr = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDR);
	hipctdd = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCTDD);
	hipci = snd_sof_dsp_read(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR);

	/* reply message from DSP */
	if (hipcida & CNL_DSP_REG_HIPCIDA_DONE) {
		msg_ext = hipci & CNL_DSP_REG_HIPCIDR_MSG_MASK;
		msg = hipcida & CNL_DSP_REG_HIPCIDA_MSG_MASK;

		dev_vdbg(sdev->dev,
				"ipc: firmware response, msg:0x%x, msg_ext:0x%x\n",
				msg, msg_ext);

		/* mask Done interrupt */
		snd_sof_dsp_update_bits(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCCTL,
					CNL_DSP_REG_HIPCCTL_DONE, 0);

		spin_lock_irq(&sdev->ipc_lock);
		ipc4_cavs_dsp_done(sdev);
		spin_unlock_irq(&sdev->ipc_lock);

		ipc_irq = true;
	}

	/* new message from DSP */
	if (hipctdr & CNL_DSP_REG_HIPCTDR_BUSY) {
		msg = hipctdr & CNL_DSP_REG_HIPCTDR_MSG_MASK;
		msg_ext = hipctdd & CNL_DSP_REG_HIPCTDD_MSG_MASK;

		dev_vdbg(sdev->dev, "ipc: firmware initiated, msg:0x%x, msg_ext:0x%x\n",
			 msg, msg_ext);

		/*
		 * cavs fw sends a new fw ipc message to host to
		 * notify the status of the last host ipc message
		 */
		if (hipctdr & SOF_IPC4_GLB_MSG_DIR_MASK)
			sof_ipc4_check_reply_status(sdev, msg);
		else
			snd_sof_ipc4_msgs_rx(sdev, msg, msg_ext);

		ipc4_cavs_host_done(sdev);
		ipc_irq = true;
	}

	if (!ipc_irq) {
		/*
		 * This interrupt is not shared so no need to return IRQ_NONE.
		 */
		dev_dbg_ratelimited(sdev->dev, "nothing to do in IPC IRQ thread\n");
	}

	return IRQ_HANDLED;
}

int sof_ipc4_cavs_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	/* send the message via mailbox */
	if (msg->msg_size)
		sof_mailbox_write(sdev, sdev->host_box.offset, msg->msg_data, msg->msg_size);

	snd_sof_dsp_write(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDD, msg->extension);
	snd_sof_dsp_write(sdev, HDA_DSP_BAR, CNL_DSP_REG_HIPCIDR,
			  msg->header | CNL_DSP_REG_HIPCIDR_BUSY);

	return 0;
}

u32 sof_ipc4_cavs_dsp_get_ipc_version(struct snd_sof_dev *sdev)
{
	return SOF_IPC_VERSION_2;
}

static int generate_copier_config(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
			struct snd_pcm_hw_params *params,
			struct sof_ipc_pcm_params *ipc_params,
			int lp_mode)
{
	struct snd_sof_widget *w = (struct snd_sof_widget *)swidget;
	struct sof_module_processor *processor;
	struct sof_ipc4_module_copier *copier;
	struct sof_gtw_attributes *gtw_attr;
	int module_id;
	int ret = 0;
	int type;

	dev_dbg(sdev->dev, "generate copier config for widget %s type %d",
				swidget->widget->name, swidget->id);

	module_id = SOF_IPC4_MODULE_ID(swidget->comp_id);
	processor = sdev->fw_modules[module_id].private;

	switch (swidget->id) {
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_aif_out:
	{
		struct sof_ipc4_host *host = (struct sof_ipc4_host *)swidget->private;

		copier = &host->copier;

		gtw_attr = devm_kzalloc(sdev->dev, sizeof(*gtw_attr), GFP_KERNEL);
		if (!gtw_attr)
			return -ENOMEM;

		gtw_attr->lp_buffer_alloc = lp_mode;
		copier->gtw_cfg.config_length = sizeof(*gtw_attr) >> 2;
		host->copier_config = (uint32_t *)gtw_attr;

		if (swidget->id == snd_soc_dapm_aif_in) {
			type = nHdaHostOutputClass;
		} else {
			type = nHdaHostInputClass;
			copier->base_config.audio_fmt.bit_depth = 32;
		}

		copier->gtw_cfg.node_id = SOF_IPC4_NODE_INDEX(ipc_params->params.stream_tag - 1) |
			SOF_IPC4_NODE_TYPE(type);
		copier->gtw_cfg.dma_buffer_size = copier->base_config.obs;

		break;
	}
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
	{
		struct sof_ipc4_dai *ipc4_dai;
		struct snd_sof_dai *dai;
		int bit_depth, valid_bits;
		int rate, channels;

		ipc4_dai = (struct sof_ipc4_dai *)swidget->private;
		copier = &ipc4_dai->copier;
		dai = &ipc4_dai->dai;

		channels = params_channels(params);
		rate = params_rate(params);

		switch (ipc4_dai->dai.dai_config->type) {
		case SOF_DAI_INTEL_ALH:
			if (w->id == snd_soc_dapm_dai_in) {
				copier->out_format.bit_depth = 32;
				copier->base_config.obs =
					sof_ipc4_module_buffer_size(channels, rate, 32,
								    processor->sch_num);
				copier->gtw_cfg.dma_buffer_size = copier->base_config.obs;
				type = nALHLinkOutputClass;
			} else {
				copier->base_config.ibs =
					sof_ipc4_module_buffer_size(channels, rate, 32,
								    processor->sch_num);
				copier->gtw_cfg.dma_buffer_size = copier->base_config.ibs;
				type = nALHLinkInputClass;
			}

			copier->gtw_cfg.node_id =
				SOF_IPC4_NODE_INDEX(ipc4_dai->dai.dai_config->alh.stream_id) |
				SOF_IPC4_NODE_TYPE(type);
			ret = sof_ipc4_generate_alh_blob(sdev, ipc4_dai, lp_mode);
			break;
		case SOF_DAI_INTEL_SSP:
			{
				valid_bits = dai->dai_config->ssp.sample_valid_bits;
				bit_depth = dai->dai_config->ssp.tdm_slot_width;
				channels = params_channels(params);
				rate = params_rate(params);

				if (swidget->id == snd_soc_dapm_dai_in) {
					copier->out_format.bit_depth = bit_depth;
					copier->out_format.valid_bit_depth = valid_bits;
					copier->base_config.obs =
						sof_ipc4_module_buffer_size(channels, rate,
									    bit_depth,
									    processor->sch_num);
					copier->gtw_cfg.dma_buffer_size = copier->base_config.obs;
					type = nI2sLinkOutputClass;
				} else {
					copier->base_config.audio_fmt.bit_depth = bit_depth;
					copier->base_config.audio_fmt.valid_bit_depth = valid_bits;
					copier->base_config.ibs =
						sof_ipc4_module_buffer_size(channels, rate,
									    bit_depth,
									    processor->sch_num);
					copier->gtw_cfg.dma_buffer_size = copier->base_config.ibs;
					type = nI2sLinkInputClass;
				}
			}

			copier->gtw_cfg.node_id =
				SOF_IPC4_NODE_INDEX(ipc4_dai->dai.dai_config->dai_index) |
				SOF_IPC4_NODE_TYPE(type);

			ret = sof_ipc4_generate_ssp_blob(sdev, ipc4_dai, lp_mode);
			break;
		case SOF_DAI_INTEL_DMIC:
		case SOF_DAI_INTEL_HDA:
			break;
		default:
			break;
		}

		break;
	}
	default:
		break;
	}

	return ret;
}

struct sof_module_config_generator gen_config[] = {
	{
		{0x83, 0xC, 0xA0, 0x9B, 0x12, 0xCA, 0x83, 0x4A,
		 0x94, 0x3C, 0x1F, 0xA2, 0xE8, 0x2F, 0x9D, 0xDA},
		generate_copier_config
	},
};

int sof_ipc4_get_module_config(struct snd_sof_dev *sdev,
			struct snd_pcm_hw_params *params,
			struct sof_ipc_pcm_params *ipc_params,
			void *pdata)
{
	struct snd_sof_widget *swidget = pdata;
	struct sof_ipc4_pipeline *pipeline;
	int lp_mode;
	int i;

	pipeline = (struct sof_ipc4_pipeline *)swidget->pipe_widget->private;
	lp_mode = pipeline->lp_mode;
	for (i = 0; i < sizeof(gen_config) / sizeof(struct sof_module_config_generator); i++) {
		if (!memcmp(&gen_config[i].uuid, &swidget->comp_ext.uuid, UUID_SIZE))
			return gen_config[i].generate(sdev, swidget, params, ipc_params, lp_mode);
	}

	return -EINVAL;
}
