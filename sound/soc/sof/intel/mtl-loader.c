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
int mtl_fw_ext_man_parse(struct snd_sof_dev *sdev, const struct firmware *fw)
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

