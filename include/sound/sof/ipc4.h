/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2020 Intel Corporation. All rights reserved.
 */

#ifndef __INCLUDE_SOUND_SOF_IPC4_H__
#define __INCLUDE_SOUND_SOF_IPC4_H__

/* ipc2 notification msg */
#define SOF_IPC4_GLB_NOTIFY_TYPE_SHIFT	16
#define SOF_IPC4_GLB_NOTIFY_TYPE_MASK	0xFF
#define SOF_IPC4_GLB_NOTIFY_TYPE(x)		(((x) >> SOF_IPC4_GLB_NOTIFY_TYPE_SHIFT) \
						& SOF_IPC4_GLB_NOTIFY_TYPE_MASK)

#define SOF_IPC4_GLB_NOTIFY_MSG_TYPE_SHIFT	24
#define SOF_IPC4_GLB_NOTIFY_MSG_TYPE_MASK	0x1F
#define SOF_IPC4_GLB_NOTIFY_MSG_TYPE(x)	(((x) >> SOF_IPC4_GLB_NOTIFY_MSG_TYPE_SHIFT)	\
						& SOF_IPC4_GLB_NOTIFY_MSG_TYPE_MASK)

enum ipc4_notification_type {
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
