/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 *
 * Author: Rander Wang <rander.wang@linux.intel.com>
 */

#ifndef __SOF_IPC4_INTEL_H
#define __SOF_IPC4_INTEL_H

#include "../sof-audio.h"
#include "../ipc4-topology.h"

#define SET_BIT(b, x)		(((x) & 1) << (b))
#define SET_BITS(b_hi, b_lo, x)	\
	(((x) & ((1ULL << ((b_hi) - (b_lo) + 1ULL)) - 1ULL)) << (b_lo))

#define IPC4_CAVS_MBOX_UPLINK_SIZE      0x1000
#define IPC4_CAVS_MBOX_DOWNLINK_SIZE    0x1000

#define IPC4_DBOX_DEFAULT_SIZE	0x2000
#define IPC4_DBOX_CAVS_25_SIZE	0x10000

enum sof_node_type {
	//HD/A host output (-> DSP).
	nHdaHostOutputClass = 0,
	//HD/A host input (<- DSP).
	nHdaHostInputClass = 1,
	//HD/A host input/output (rsvd for future use).
	nHdaHostInoutClass = 2,

	//HD/A link output (DSP ->).
	nHdaLinkOutputClass = 8,
	//HD/A link input (DSP <-).
	nHdaLinkInputClass = 9,
	//HD/A link input/output (rsvd for future use).
	nHdaLinkInoutClass = 10,

	//DMIC link input (DSP <-).
	nDmicLinkInputClass = 11,

	//I2S link output (DSP ->).
	nI2sLinkOutputClass = 12,
	//I2S link input (DSP <-).
	nI2sLinkInputClass = 13,

	//ALH link output, legacy for SNDW (DSP ->).
	nALHLinkOutputClass = 16,
	//ALH link input, legacy for SNDW (DSP <-).
	nALHLinkInputClass = 17,

	//SNDW link output (DSP ->).
	nAlhSndWireStreamLinkOutputClass = 16,
	//SNDW link input (DSP <-).
	nAlhSndWireStreamLinkInputClass = 17,

	//UAOL link output (DSP ->).
	nAlhUAOLStreamLinkOutputClass = 18,
	//UAOL link input (DSP <-).
	nAlhUAOLStreamLinkInputClass = 19,

	//IPC output (DSP ->).
	nIPCOutputClass = 20,
	//IPC input (DSP <-).
	nIPCInputClass = 21,

	//I2S Multi gtw output (DSP ->).
	nI2sMultiLinkOutputClass = 22,
	//I2S Multi gtw input (DSP <-).
	nI2sMultiLinkInputClass = 23,
	//GPIO
	nGpioClass = 24,
	//SPI
	nSpiOutputClass = 25,
	nSpiInputClass = 26,
	nMaxConnectorNodeIdType
};

#define SOF_IPC4_NODE_INDEX(x)	(x)
#define SOF_IPC4_NODE_TYPE(x)	((x) << 8)

struct sof_gtw_attributes {
	// Gateway data requested in low power memory.
	uint32_t lp_buffer_alloc : 1;

	//Gateway data requested in register file memory.
	uint32_t alloc_from_reg_file : 1;

	// Reserved field
	uint32_t _rsvd : 30;
} __packed;

struct sof_module_config_generator {
	u8 uuid[UUID_SIZE];
	int (*generate)(struct snd_sof_dev *sdev, struct snd_sof_widget *swidget,
		struct snd_pcm_hw_params *params, struct sof_ipc_pcm_params *ipc_params,
		int lp_mode);
};

int snd_sof_fw_ext_man_parse_cavs(struct snd_sof_dev *sdev,
				  const struct firmware *fw);

irqreturn_t sof_ipc4_cavs_irq_thread(int irq, void *context);
int sof_ipc4_cavs_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg);
u32 sof_ipc4_cavs_dsp_get_ipc_version(struct snd_sof_dev *sdev);

int sof_cavs_fw_ready(struct snd_sof_dev *sdev, u32 msg_id);

int sof_ipc4_get_module_config(struct snd_sof_dev *sdev,
			struct snd_pcm_hw_params *params,
			struct sof_ipc_pcm_params *ipc_params,
			void *pdata);

int sof_ipc4_generate_dmic_config(struct snd_sof_dev *sdev, struct sof_ipc4_dai *ipc4_dai,
		struct snd_pcm_hw_params *params,
		int lp_mode);

int copy_nhlt_blob(uint8_t *blob, size_t size);
#endif
