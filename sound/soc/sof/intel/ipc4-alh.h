/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 *
 * Author: Bard Liao <yung-chuan.liao@linux.intel.com>
 */

#ifndef __SOF_INTEL_IPC4_ALH_H
#define __SOF_INTEL_IPC4_ALH_H

#include "ipc4-intel.h"

#define ALH_MAX_NUMBER_OF_GTW   1

struct AlhMultiGtwCfg {
	//! Number of single channels (valid items in mapping array).
	uint32_t count;
	//! Single to multi aggregation mapping item.
	struct {
		//! Vindex of a single ALH channel aggregated.
		uint32_t alh_id;
		//! Channel mask
		uint32_t channel_mask;
	} mapping[ALH_MAX_NUMBER_OF_GTW]; //!< Mapping items
} __packed;

struct sof_alh_configuration_blob {
	struct sof_gtw_attributes gw_attr;
	struct AlhMultiGtwCfg alh_cfg;
};


#define SNDW_MULTI_GTW_BASE  0x50

int sof_ipc4_generate_alh_blob(struct snd_sof_dev *sdev,
				struct sof_ipc4_dai *ipc4_dai,
				int lp_mode);
#endif
