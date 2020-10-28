/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_HEADER2_H__
#define __INCLUDE_SOUND_SOF_HEADER2_H__

#include <linux/types.h>
#include <uapi/sound/sof/abi.h>

/** \addtogroup sof_uapi uAPI
 *  SOF uAPI specification.
 *  @{
 */

/*
 * IPC4 messages have two 32 bit identifier made up as follows :-
 *
 * header - msg type, msg id, msg direction ...
 * extension - extra params such as msg data size in mailbox
 *
 * These are sent at the start of the IPC message in the mailbox. Messages
 * should not be sent in the doorbell (special exceptions for firmware).
 */

enum MsgTarget {
	/* Global FW message */
	FW_GEN_MSG = 0,

	/* Module message */
	MODULE_MSG = 1
};

enum global_msg {
	GLB_BOOT_CONFIG = 0,
	GLB_ROM_CONTROL = 1,
	GLB_IPCGATEWAY_CMD = 2,

	/* Create EDF task and run RTOS instance in it */
	GLB_START_RTOS_EDF_TASK = 3,
	/* Stop RTOS and delete its EDF task context */
	GLB_STOP_RTOS_EDF_TASK = 4,

	GLB_PERF_MEASUREMENTS_CMD = 13,
	GLB_CHAIN_DMA = 14,

	GLB_LOAD_MULTIPLE_MODULES = 15,
	GLB_UNLOAD_MULTIPLE_MODULES = 16,

	/* pipeline settings */
	GLB_CREATE_PIPELINE = 17,
	GLB_DELETE_PIPELINE = 18,
	GLB_SET_PIPELINE_STATE = 19,
	GLB_GET_PIPELINE_STATE = 20,
	GLB_GET_PIPELINE_CONTEXT_SIZE = 21,
	GLB_SAVE_PIPELINE = 22,
	GLB_RESTORE_PIPELINE = 23,

	/* Loads library (using Code Load or HD/A Host Output DMA) */
	GLB_LOAD_LIBRARY = 24,
	GLB_INTERNAL_MESSAGE = 26,

	/* Notification (FW to SW driver) */
	GLB_NOTIFICATION = 27,
	GLB_MAX_IXC_MESSAGE_TYPE = 31
};

/* Message direction */
enum MsgDir {
	MSG_REQUEST = 0,
	MSG_REPLY = 1,
};

enum PipelineState {
	PIPE_RESET = 2,
	PIPE_PAUSED = 3,
	PIPE_RUNNING = 4,
	PIPE_EOS = 5
};

/* global common ipc msg */
#define SOF_IPC4_GLB_MSG_TARGET_SHIFT	30
#define SOF_IPC4_GLB_MSG_TARGET_MASK	BIT(30)
#define SOF_IPC4_GLB_MSG_TARGET(x)	((x) << SOF_IPC4_GLB_MSG_TARGET_SHIFT)

#define SOF_IPC4_GLB_MSG_DIR_SHIFT	29
#define SOF_IPC4_GLB_MSG_DIR_MASK	BIT(29)
#define SOF_IPC4_GLB_MSG_DIR(x)	((x) << SOF_IPC4_GLB_MSG_DIR_SHIFT)

#define SOF_IPC4_GLB_MSG_TYPE_SHIFT		24
#define SOF_IPC4_GLB_MSG_TYPE_MASK		GENMASK(28, 24)
#define SOF_IPC4_GLB_MSG_TYPE(x)	((x) << SOF_IPC4_GLB_MSG_TYPE_SHIFT)

/* pipeline creation ipc msg */
#define SOF_IPC4_GLB_PIPE_INSTANCE_SHIFT	16
#define SOF_IPC4_GLB_PIPE_INSTANCE_MASK	GENMASK(23, 16)
#define SOF_IPC4_GLB_PIPE_INSTANCE_ID(x)	((x) << SOF_IPC4_GLB_PIPE_INSTANCE_SHIFT)

#define SOF_IPC4_GLB_PIPE_PRIORITY_SHIFT	11
#define SOF_IPC4_GLB_PIPE_PRIORITY_MASK	GENMASK(15, 11)
#define SOF_IPC4_GLB_PIPE_PRIORITY(x)	((x) << SOF_IPC4_GLB_PIPE_PRIORITY_SHIFT)

#define SOF_IPC4_GLB_PIPE_MEM_SIZE_SHIFT	0
#define SOF_IPC4_GLB_PIPE_MEM_SIZE_MASK	GENMASK(10, 0)
#define SOF_IPC4_GLB_PIPE_MEM_SIZE(x)	((x) << SOF_IPC4_GLB_PIPE_MEM_SIZE_SHIFT)

#define SOF_IPC4_GL_PIPE_EXT_LP_SHIFT	0
#define SOF_IPC4_GL_PIPE_EXT_LP_MASK	BIT(0)
#define SOF_IPC4_GL_PIPE_EXT_LP(x)	((x) << SOF_IPC4_GL_PIPE_EXT_LP_SHIFT)

/* pipeline set state ipc msg */
#define SOF_IPC4_GL_PIPE_STATE_TYPE_SHIFT	24
#define SOF_IPC4_GL_PIPE_STATE_TYPE_MASK	GENMASK(28, 24)
#define SOF_IPC4_GL_PIPE_STATE_TYPE(x)	((x) << SOF_IPC4_GL_PIPE_STATE_TYPE_SHIFT)

#define SOF_IPC4_GL_PIPE_STATE_ID_SHIFT	16
#define SOF_IPC4_GL_PIPE_STATE_ID_MASK	GENMASK(23, 16)
#define SOF_IPC4_GL_PIPE_STATE_ID(x)	((x) << SOF_IPC4_GL_PIPE_STATE_ID_SHIFT)

#define SOF_IPC4_GL_PIPE_STATE_SHIFT	0
#define SOF_IPC4_GL_PIPE_STATE_MASK	GENMASK(15, 0)
#define SOF_IPC4_GL_PIPE_STATE(x) ((x) << SOF_IPC4_GL_PIPE_STATE_SHIFT)

enum module_type {
	MOD_INIT_INSTANCE		= 0,
	MOD_CONFIG_GET			= 1,
	MOD_CONFIG_SET			= 2,
	MOD_LARGE_CONFIG_GET		= 3,
	MOD_LARGE_CONFIG_SET		= 4,
	MOD_BIND			= 5,
	MOD_UNBIND			= 6,
	MOD_SET_DX			= 7,
	MOD_SET_D0IX			= 8,
	MOD_ENTER_MODULE_RESTORE	= 9,
	MOD_EXIT_MODULE_RESTORE		= 10,
	MOD_DELETE_INSTANCE		= 11,
};

enum sampling_frequency {
	FS_8000HZ	= 8000,
	FS_11025HZ	= 11025,
	FS_12000HZ	= 12000, /* Mp3, AAC, SRC only. */
	FS_16000HZ	= 16000,
	FS_18900HZ	= 18900, /* SRC only for 44100 */
	FS_22050HZ	= 22050,
	FS_24000HZ	= 24000, /* Mp3, AAC, SRC only. */
	FS_32000HZ	= 32000,
	FS_37800HZ	= 37800, /* SRC only for 44100 */
	FS_44100HZ	= 44100,
	FS_48000HZ	= 48000, /* Default. */
	FS_64000HZ	= 64000, /* AAC, SRC only. */
	FS_88200HZ	= 88200, /* AAC, SRC only. */
	FS_96000HZ	= 96000, /* AAC, SRC only. */
	FS_176400HZ	= 176400, /* SRC only. */
	FS_192000HZ	= 192000, /* SRC only. */
	FS_INVALID
};

enum bit_depth {
	DEPTH_8BIT	= 8, /* 8 bits depth */
	DEPTH_16BIT	= 16, /* 16 bits depth */
	DEPTH_24BIT	= 24, /* 24 bits depth - Default */
	DEPTH_32BIT	= 32, /* 32 bits depth */
	DEPTH_64BIT	= 64, /* 64 bits depth */
	DEPTH_INVALID
};

enum channel_config {
	/* one channel only. */
	CHANNEL_CONFIG_MONO			= 0,
	/* L & R. */
	CHANNEL_CONFIG_STEREO			= 1,
	/* L, R & LFE; PCM only. */
	CHANNEL_CONFIG_2_POINT_1		= 2,
	/* L, C & R; MP3 & AAC only. */
	CHANNEL_CONFIG_3_POINT_0		= 3,
	/* L, C, R & LFE; PCM only. */
	CHANNEL_CONFIG_3_POINT_1		= 4,
	/* L, R, Ls & Rs; PCM only. */
	CHANNEL_CONFIG_QUATRO			= 5,
	/* L, C, R & Cs; MP3 & AAC only. */
	CHANNEL_CONFIG_4_POINT_0		= 6,
	/* L, C, R, Ls & Rs. */
	CHANNEL_CONFIG_5_POINT_0		= 7,
	/* L, C, R, Ls, Rs & LFE. */
	CHANNEL_CONFIG_5_POINT_1		= 8,
	/* one channel replicated in two. */
	CHANNEL_CONFIG_DUAL_MONO		= 9,
	/* Stereo (L,R) in 4 slots, 1st stream: [ L, R, -, - ] */
	CHANNEL_CONFIG_I2S_DUAL_STEREO_0	= 10,
	/* Stereo (L,R) in 4 slots, 2nd stream: [ -, -, L, R ] */
	CHANNEL_CONFIG_I2S_DUAL_STEREO_1	= 11,
	/* L, C, R, Ls, Rs & LFE., LS, RS */
	CHANNEL_CONFIG_7_POINT_1		= 12,
	CHANNEL_CONFIG_INVALID
};

enum interleaved_style {
	CHANNELS_INTERLEAVED = 0,
	CHANNELS_NONINTERLEAVED = 1,
};

enum sample_type {
	MSB_INTEGER = 0, /* integer with Most Significant Byte first */
	LSB_INTEGER = 1, /* integer with Least Significant Byte first */
	SIGNED_INTEGER = 2, /* signed integer */
	UNSIGNED_INTEGER = 3, /* unsigned integer */
	FLOAT = 4 /* unsigned integer */
};

struct audio_format {
	enum sampling_frequency sampling_frequency;
	enum bit_depth bit_depth;
	uint32_t ch_map;
	enum channel_config ch_cfg;
	uint32_t interleaving_style;
	uint32_t channels_count : 8;
	uint32_t valid_bit_depth : 8;
	enum sample_type s_type : 8;
	uint32_t reserved : 8;
};

struct basic_module_cfg {
	uint32_t cpc;	/* the max count of Cycles Per Chunk processing */
	uint32_t ibs;	/* input Buffer Size (in bytes)  */
	uint32_t obs;  /* output Buffer Size (in bytes) */
	uint32_t is_pages;	/* number of physical pages used */
	struct audio_format audio_fmt;
};

enum node_type {
	/* HD/A host output (-> DSP) */
	kHdaHostOutputClass = 0,
	/* HD/A host input (<- DSP) */
	kHdaHostInputClass = 1,
	/* HD/A host input/output (rsvd for future use) */
	kHdaHostInoutClass = 2,

	/* HD/A link output (DSP ->) */
	kHdaLinkOutputClass = 8,
	/* HD/A link input (DSP <-) */
	kHdaLinkInputClass = 9,
	/* HD/A link input/output (rsvd for future use) */
	kHdaLinkInoutClass = 10,

	/* DMIC link input (DSP <-) */
	kDmicLinkInputClass = 11,

	/* I2S link output (DSP ->) */
	kI2sLinkOutputClass = 12,
	/* I2S link input (DSP <-) */
	kI2sLinkInputClass = 13,

	/* ALH link output, legacy for SNDW (DSP ->) */
	kALHLinkOutputClass = 16,
	/* ALH link input, legacy for SNDW (DSP <-) */
	kALHLinkInputClass = 17,

	/* SNDW link output (DSP ->) */
	kAlhSndWireStreamLinkOutputClass = 16,
	/* SNDW link input (DSP <-) */
	kAlhSndWireStreamLinkInputClass = 17,

	/* UAOL link output (DSP ->) */
	kAlhUAOLStreamLinkOutputClass = 18,
	/* UAOL link input (DSP <-) */
	kAlhUAOLStreamLinkInputClass = 19,

	/* IPC output (DSP ->) */
	kIPCOutputClass = 20,
	/* IPC input (DSP <-) */
	kIPCInputClass = 21,

	/* I2S Multi gtw output (DSP ->) */
	kI2sMultiLinkOutputClass = 22,
	/* I2S Multi gtw input (DSP <-) */
	kI2sMultiLinkInputClass = 23,
	/* GPIO */
	kGpioClass = 24,
	/* SPI */
	kSpiOutputClass = 25,
	kSpiInputClass = 26,
	kMaxConnectorNodeIdType
};

//! Invalid raw node id (to indicate uninitialized node id).
#define kInvalidNodeId      0xffffffff

struct node_id {
	uint32_t channel	: 8;	/* dma channel number */
	enum node_type type	: 5;
	uint32_t _rsvd		: 19;
};

struct copier_gateway_cfg {
	/* ID of Gateway Node */
	struct node_id node;

	/* referred Gateway DMA buffer size (in bytes) */
	uint32_t dma_buffer_size;

	/* length of gateway node configuration blob specified in config_data */
	uint32_t config_length;
	/* gateway node configuration blob */
	uint32_t config_data[0];
};

struct copier_module_cfg {
	/* audio input buffer format */
	struct basic_module_cfg basic_cfg;

	/* audio format for output */
	struct audio_format out_fmt;

	uint32_t copier_feature_mask;
	struct copier_gateway_cfg  gtw_cfg;
};

/* common module ipc msg */
#define SOF_IPC4_MOD_INSTANCE_SHIFT	16
#define SOF_IPC4_MOD_INSTANCE_MASK	GENMASK(23, 16)
#define SOF_IPC4_MOD_INSTANCE(x)	((x) << SOF_IPC4_MOD_INSTANCE_SHIFT)

#define SOF_IPC4_MOD_ID_SHIFT		0
#define SOF_IPC4_MOD_ID_MASK		GENMASK(15, 0)
#define SOF_IPC4_MOD_ID(x)		((x) << SOF_IPC4_MOD_ID_SHIFT)

/* init module ipc msg */
#define SOF_IPC4_MOD_EXT_PARAM_SIZE_SHIFT	0
#define SOF_IPC4_MOD_EXT_PARAM_SIZE_MASK	GENMASK(15, 0)
#define SOF_IPC4_MOD_EXT_PARAM_SIZE(x)		((x) << SOF_IPC4_MOD_EXT_PARAM_SIZE_SHIFT)

#define SOF_IPC4_MOD_EXT_PPL_ID_SHIFT	16
#define SOF_IPC4_MOD_EXT_PPL_ID_MASK	GENMASK(23, 16)
#define SOF_IPC4_MOD_EXT_PPL_ID(x)	((x) << SOF_IPC4_MOD_EXT_PPL_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_CORE_ID_SHIFT	24
#define SOF_IPC4_MOD_EXT_CORE_ID_MASK	GENMASK(27, 24)
#define SOF_IPC4_MOD_EXT_CORE_ID(x)	((x) << SOF_IPC4_MOD_EXT_CORE_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_DOMAIN_SHIFT	28
#define SOF_IPC4_MOD_EXT_DOMAIN_MASK	BIT(28)
#define SOF_IPC4_MOD_EXT_DOMAIN(x)	((x) << SOF_IPC4_MOD_EXT_DOMAIN_SHIFT)

/*  bind/unbind module ipc msg */
#define SOF_IPC4_MOD_EXT_DST_MOD_ID_SHIFT	0
#define SOF_IPC4_MOD_EXT_DST_MOD_ID_MASK	GENMASK(15, 0)
#define SOF_IPC4_MOD_EXT_DST_MOD_ID(x)		((x) << SOF_IPC4_MOD_EXT_DST_MOD_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE_SHIFT	16
#define SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE_MASK	GENMASK(23, 16)
#define SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(x)	((x) << SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE_SHIFT)

#define SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID_SHIFT	24
#define SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID_MASK	GENMASK(26, 24)
#define SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(x)	((x) << SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID_SHIFT	27
#define SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID_MASK	GENMASK(29, 27)
#define SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(x)	((x) << SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID_SHIFT)

#define MOD_ENABLE_LOG	6
#define MOD_SYSTEM_TIME	20

/* set module large config */
#define SOF_IPC4_MOD_EXT_MSG_SIZE_SHIFT	0
#define SOF_IPC4_MOD_EXT_MSG_SIZE_MASK	GENMASK(19, 0)
#define SOF_IPC4_MOD_EXT_MSG_SIZE(x)	((x) << SOF_IPC4_MOD_EXT_MSG_SIZE_SHIFT)

#define SOF_IPC4_MOD_EXT_MSG_PARAM_ID_SHIFT	20
#define SOF_IPC4_MOD_EXT_MSG_PARAM_ID_MASK	GENMASK(27, 20)
#define SOF_IPC4_MOD_EXT_MSG_PARAM_ID(x)	((x) << SOF_IPC4_MOD_EXT_MSG_PARAM_ID_SHIFT)

#define SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK_SHIFT	28
#define SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK_MASK	BIT(28)
#define SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK(x)	((x) << SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK_SHIFT)

#define SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK_SHIFT	29
#define SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK_MASK	BIT(29)
#define SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK(x)	((x) << SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK_SHIFT)

/* ipc4 notification msg */
#define SOF_IPC4_GLB_NOTIFY_TYPE_SHIFT	16
#define SOF_IPC4_GLB_NOTIFY_TYPE_MASK	0xFF
#define SOF_IPC4_GLB_NOTIFY_TYPE(x)		(((x) >> SOF_IPC4_GLB_NOTIFY_TYPE_SHIFT) \
						& SOF_IPC4_GLB_NOTIFY_TYPE_MASK)

#define SOF_IPC4_GLB_NOTIFY_MSG_TYPE_SHIFT	24
#define SOF_IPC4_GLB_NOTIFY_MSG_TYPE_MASK	0x1F
#define SOF_IPC4_GLB_NOTIFY_MSG_TYPE(x)	(((x) >> SOF_IPC4_GLB_NOTIFY_MSG_TYPE_SHIFT)	\
						& SOF_IPC4_GLB_NOTIFY_MSG_TYPE_MASK)

enum cavs_ipc_notification_type {
	SOF_IPC4_GLB_NOTIFY_PHRASE_DETECTED = 4,
	SOF_IPC4_GLB_NOTIFY_RESOURCE_EVENT = 5,
	SOF_IPC4_GLB_NOTIFY_LOG_BUFFER_STATUS = 6,
	SOF_IPC4_GLB_NOTIFY_TIMESTAMP_CAPTURED = 7,
	SOF_IPC4_GLB_NOTIFY_FW_READY = 8
};

#define SOF_IPC4_GLB_NOTIFY_DIR_MASK	BIT(29)
#define SOF_IPC4_REPLY_STATUS_MASK	GENMASK(23, 0)

/** @}*/

#endif
