// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// Copyright(c) 2020 Intel Corporation. All rights reserved.
//
// Authors: Rander Wang <rander.wang@linux.intel.com>
//

/*
 * FW loader for Meteorlake.
 */

#include <linux/firmware.h>
#include <sound/sof.h>
#include <sound/sof/cavs_ext_manifest.h>
#include "../sof-audio.h"
#include "hda.h"
#include "ipc4-intel.h"
#include "../ops.h"
#include "../ipc4.h"

/*********************************************************************
 *     css_manifest hdr
 *-------------------
 *     offset reserved for future
 *-------------------
 *     fw_hdr
 *-------------------
 *     module_entry[0]
 *-------------------
 *     module_entry[1]
 *-------------------
 *     ...
 *-------------------
 *     module_entry[n]
 *-------------------
 *     FW content
 *-------------------
 *********************************************************************/
int snd_sof_fw_ext_man_parse_cavs(struct snd_sof_dev *sdev,
				  const struct firmware *fw)
{
	struct sof_ipc4_fw_modules *module_entry;
	struct CavsFwBinaryHeader *fw_header;
	struct cavs_ext_manifest_hdr *hdr;
	struct ModuleConfig *fm_config;
	struct ModuleEntry *fm_entry;
	int fw_offset;
	int i;

	if (fw->size < sizeof(hdr)) {
		dev_err(sdev->dev, "Invalid fw size\n");
		return -EINVAL;
	}

	hdr = (struct cavs_ext_manifest_hdr *)fw->data;

	if (hdr->id == CAVS_EXT_MAN_MAGIC_NUMBER) {
		fw_offset = hdr->len;
	} else {
		dev_err(sdev->dev, "invalid cavs FW");
		return -EINVAL;
	}

	fw_header = (struct CavsFwBinaryHeader *)(fw->data + fw_offset + CAVS18_FW_HDR_OFFSET);
	dev_dbg(sdev->dev, " fw %s: header length %x, module num %d", fw_header->name,
		fw_header->len, fw_header->num_module_entries);

	fm_entry = (struct ModuleEntry *)((void *)fw_header + fw_header->len);
	fm_config = (struct ModuleConfig *)(fm_entry + fw_header->num_module_entries);

	module_entry = devm_kmalloc_array(sdev->dev, fw_header->num_module_entries,
					  sizeof(*module_entry), GFP_KERNEL);
	if (!module_entry)
		return -ENOMEM;


	sdev->fw_module_num = fw_header->num_module_entries;
	sdev->fw_modules = module_entry;

	for (i = 0; i < fw_header->num_module_entries; i++) {
		int dw_count;

		dev_err(sdev->dev, "module %s : UUID %pUL, ", fm_entry->name,
			fm_entry->uuid);

		memcpy(module_entry->uuid, fm_entry->uuid, UUID_SIZE);
		memcpy(module_entry->name, fm_entry->name, MAX_MODULE_NAME_LEN);

		if (fm_entry->cfg_count)
			module_entry->bss_size = fm_config[fm_entry->cfg_offset].is_bytes;

		memcpy(&module_entry->type, &fm_entry->type, sizeof(fm_entry->type));

		/* bringup fw starts at zero */
		module_entry->id = i;
		module_entry->instance_max_count = fm_entry->instance_max_count;

		/* align to dw */
		dw_count = (module_entry->instance_max_count + 31) >> 5;
		module_entry->instance_id = devm_kzalloc(sdev->dev, dw_count, GFP_KERNEL);
		if (!module_entry->instance_id)
			return -ENOMEM;

		module_entry++;
		fm_entry++;
	}

	return fw_offset;
}

int sof_cavs_fw_ready(struct snd_sof_dev *sdev, u32 msg_id)
{
	int inbox_offset, inbox_size;
	int outbox_offset, outbox_size;

	/* mailbox must be on 4k boundary */
	inbox_offset = snd_sof_dsp_get_mailbox_offset(sdev);
	if (inbox_offset < 0) {
		dev_err(sdev->dev, "error: have no mailbox offset\n");
		return inbox_offset;
	}

	dev_dbg(sdev->dev, "ipc: DSP is ready 0x%8.8x offset 0x%x\n",
		msg_id, inbox_offset);

	/* no need to re-check version/ABI for subsequent boots */
	if (!sdev->first_boot)
		return 0;

	inbox_size = IPC4_CAVS_MBOX_UPLINK_SIZE;
	outbox_offset = snd_sof_dsp_get_window_offset(sdev, 1);
	outbox_size = IPC4_CAVS_MBOX_DOWNLINK_SIZE;

	sdev->dsp_box.offset = inbox_offset;
	sdev->dsp_box.size = inbox_size;
	sdev->host_box.offset = outbox_offset;
	sdev->host_box.size = outbox_size;

	dev_dbg(sdev->dev, " mailbox upstream 0x%x - size 0x%x\n",
		inbox_offset, inbox_size);
	dev_dbg(sdev->dev, " mailbox downstream 0x%x - size 0x%x\n",
		outbox_offset, outbox_size);


	return 0;
}

