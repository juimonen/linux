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

int snd_sof_fw_ext_man_parse_cavs(struct snd_sof_dev *sdev,
				  const struct firmware *fw);

irqreturn_t sof_ipc4_cavs_irq_thread(int irq, void *context);
int sof_ipc4_cavs_send_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg);
u32 sof_ipc4_cavs_dsp_get_ipc_version(struct snd_sof_dev *sdev);

#endif
