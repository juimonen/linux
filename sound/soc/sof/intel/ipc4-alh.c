// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 *
 * Author: Rander Wang <rander.wang@linux.intel.com>
 */

#include <sound/sof/dai-intel.h>
#include "../sof-audio.h"
#include "../ipc4-topology.h"
#include "../ipc4.h"
#include "ipc4-alh.h"

static int sof_ipc4_generate_alh_config(struct snd_sof_dev *sdev, struct sof_ipc4_dai *ipc4_dai,
				struct sof_alh_configuration_blob *blob)
{
	struct sof_ipc_dai_alh_params *alh;

	alh = &ipc4_dai->dai.dai_config->alh;
	blob->alh_cfg.count = 1;
	blob->alh_cfg.mapping[0].alh_id = ipc4_dai->copier.gtw_cfg.node_id;
	blob->alh_cfg.mapping[0].channel_mask = 0x3;

	return 0;
}

int sof_ipc4_generate_alh_blob(struct snd_sof_dev *sdev, struct sof_ipc4_dai *ipc4_dai, int lp_mode)
{
	struct sof_alh_configuration_blob *blob;

	blob = devm_kzalloc(sdev->dev, sizeof(*blob), GFP_KERNEL);
	blob->gw_attr.lp_buffer_alloc = lp_mode;

	sof_ipc4_generate_alh_config(sdev, ipc4_dai, blob);
	ipc4_dai->copier.gtw_cfg.config_length = sizeof(*blob) >> 2;
	ipc4_dai->copier_config = (uint32_t *)blob;
	return 0;
}
