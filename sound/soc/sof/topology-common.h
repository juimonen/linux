/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
// Author: Rander Wang <rander.wang@linux.intel.com>
//

#ifndef SOF_TOPOLOGY_COMMON_H
#define SOF_TOPOLOGY_COMMON_H

#define COMP_ID_UNASSIGNED		0xffffffff

/*
 * Supported Frame format types and lookup, add new ones to end of list.
 */

struct sof_frame_types {
	const char *name;
	enum sof_ipc_frame frame;
};

struct sof_dai_types {
	const char *name;
	enum sof_ipc_dai_type type;
};

/*
 * Topology Token Parsing.
 * New tokens should be added to headers and parsing tables below.
 */

struct sof_topology_token {
	u32 token;
	u32 type;
	int (*get_token)(void *elem, void *object, u32 offset, u32 size);
	u32 offset;
	u32 size;
};

struct sof_topology_token_entry {
	char *name;
	int size;
	const struct sof_topology_token *token;
};

enum sof_topology_token_index {
	SOF_TOKEN_EXT,
	SOF_TOKEN_DAI,
	SOF_TOKEN_DAI_LINK,
	SOF_TOKEN_DMIC,
	SOF_TOKEN_DMIC_PDM,
	SOF_TOKEN_SCHED,
	SOF_TOKEN_SSP
};

enum sof_ipc_frame find_format(const char *name);
enum sof_ipc_dai_type find_dai(const char *name);

int sof_parse_topology_tokens(struct snd_soc_component *scomp, void *object, int index,
			struct snd_soc_tplg_vendor_array *array, int priv_size);

static inline int get_token_u32(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = le32_to_cpu(velem->value);
	return 0;
}

static inline int get_token_u16(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	u16 *val = (u16 *)((u8 *)object + offset);

	*val = (u16)le32_to_cpu(velem->value);
	return 0;
}

static inline int get_token_uuid(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_uuid_elem *velem = elem;
	u8 *dst = (u8 *)object + offset;

	memcpy(dst, velem->uuid, UUID_SIZE);

	return 0;
}

static inline int get_token_comp_format(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = find_format(velem->string);
	return 0;
}

static inline int get_token_dai_type(void *elem, void *object, u32 offset, u32 size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = find_dai(velem->string);
	return 0;
}

int sof_parse_token_sets(struct snd_soc_component *scomp,
				void *object,
				const struct sof_topology_token *tokens,
				int count,
				struct snd_soc_tplg_vendor_array *array,
				int priv_size, int sets, size_t object_size);

int sof_parse_tokens(struct snd_soc_component *scomp,
				void *object,
				const struct sof_topology_token *tokens,
				int count,
				struct snd_soc_tplg_vendor_array *array,
				int priv_size);

void sof_dai_set_format(struct snd_soc_tplg_hw_config *hw_config,
			       struct sof_ipc_dai_config *config);
int sof_connect_dai_widget(struct snd_soc_component *scomp,
				  struct snd_soc_dapm_widget *w,
				  struct snd_soc_tplg_dapm_widget *tw,
				  struct snd_sof_dai *dai);
int spcm_bind(struct snd_soc_component *scomp, struct snd_sof_pcm *spcm, int dir);
int sof_set_comp_pipe_widget(struct snd_sof_dev *sdev, struct snd_sof_widget *pipe_widget,
				    struct snd_sof_widget *comp_swidget);
int sof_set_dai_config(struct snd_sof_dev *sdev, u32 size,
			      struct snd_soc_dai_link *link,
			      struct sof_ipc_dai_config *config);
int sof_link_ssp_load(struct snd_soc_component *scomp, int index,
			     struct snd_soc_dai_link *link,
			     struct snd_soc_tplg_link_config *cfg,
			     struct snd_soc_tplg_hw_config *hw_config,
			     struct sof_ipc_dai_config *config, int curr_conf);
int sof_link_dmic_load(struct snd_soc_component *scomp, int index,
			      struct snd_soc_dai_link *link,
			      struct snd_soc_tplg_link_config *cfg,
			      struct snd_soc_tplg_hw_config *hw_config,
			      struct sof_ipc_dai_config *config);
int sof_link_hda_load(struct snd_soc_component *scomp, int index,
			     struct snd_soc_dai_link *link,
			     struct snd_soc_tplg_link_config *cfg,
			     struct snd_soc_tplg_hw_config *hw_config,
			     struct sof_ipc_dai_config *config);
#endif //SOF_TOPOLOGY_COMMON_H
