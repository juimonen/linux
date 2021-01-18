// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Rander Wang <rander.wang@linux.intel.com>
//
// ipc4 ipc creator
//
#include <sound/sof/header.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ipc4.h"
#include "ops.h"

static struct sof_ipc4_fw_status ipc4_status[] = {
	{0, "The operation was successful"},
	{1, "Invalid parameter specified"},
	{2, "Unknown message type specified"},
	{3, "Not enough space in the IPC reply buffer to complete the request"},
	{4, "The system or resource is busy"},
	{5, "Replaced ADSP IPC PENDING (unused) - according to cAVS v0.5"},
	{6, "Unknown error while processing the request"},
	{7, "Unsupported operation requested"},
	{8, "Reserved (ADSP_STAGE_UNINITIALIZED removed)"},
	{9, "Specified resource not found"},
	{10, "A resource's ID requested to be created is already assigned"},
	{11, "Reserved (ADSP_IPC_OUT_OF_MIPS removed)"},
	{12, "Required resource is in invalid state"},
	{13, "Requested power transition failed to complete"},
	{14, "Manifest of the library being loaded is invalid"},
	{15, "Requested service or data is unavailable on the target platform"},
	{42, "Library target address is out of storage memory range"},
	{43, "Reserved"},
	{44, "Image verification by CSE failed"},
	{100, "General module management error"},
	{101, "Module loading failed"},
	{102, "Integrity check of the loaded module content failed"},
	{103, "Attempt to unload code of the module in use"},
	{104, "Other failure of module instance initialization request"},
	{105, "Reserved (ADSP_IPC_OUT_OF_MIPS removed)"},
	{106, "Reserved (ADSP_IPC_CONFIG_GET_ERROR removed)"},
	{107, "Reserved (ADSP_IPC_CONFIG_SET_ERROR removed)"},
	{108, "Reserved (ADSP_IPC_LARGE_CONFIG_GET_ERROR removed)"},
	{109, "Reserved (ADSP_IPC_LARGE_CONFIG_SET_ERROR removed)"},
	{110, "Invalid (out of range) module ID provided"},
	{111, "Invalid module instance ID provided"},
	{112, "Invalid queue (pin) ID provided"},
	{113, "Invalid destination queue (pin) ID provided"},
	{114, "Reserved (ADSP_IPC_BIND_UNBIND_DST_SINK_UNSUPPORTED removed)"},
	{115, "Reserved (ADSP_IPC_UNLOAD_INST_EXISTS removed)"},
	{116, "Invalid target code ID provided"},
	{117, "Injection DMA buffer is too small for probing the input pin"},
	{118, "Extraction DMA buffer is too small for probing the output pin"},
	{120, "Invalid ID of configuration item provided in TLV list"},
	{121, "Invalid length of configuration item provided in TLV list"},
	{122, "Invalid structure of configuration item provided"},
	{140, "Initialization of DMA Gateway failed"},
	{141, "Invalid ID of gateway provided"},
	{142, "Setting state of DMA Gateway failed"},
	{143, "DMA_CONTROL message targeting gateway not allocated yet"},
	{150, "Attempt to configure SCLK while I2S port is running"},
	{151, "Attempt to configure MCLK while I2S port is running"},
	{152, "Attempt to stop SCLK that is not running"},
	{153, "Attempt to stop MCLK that is not running"},
	{160, "Reserved (ADSP_IPC_PIPELINE_NOT_INITIALIZED removed)"},
	{161, "Reserved (ADSP_IPC_PIPELINE_NOT_EXIST removed)"},
	{162, "Reserved (ADSP_IPC_PIPELINE_SAVE_FAILED removed)"},
	{163, "Reserved (ADSP_IPC_PIPELINE_RESTORE_FAILED removed)"},
	{164, "Reverted for ULP purposes"},
	{165, "Reserved (ADSP_IPC_PIPELINE_ALREADY_EXISTS removed)"},
};

void sof_ipc4_check_reply_status(struct snd_sof_dev *sdev, u32 msg)
{
	u32 status = msg & SOF_IPC4_REPLY_STATUS_MASK;
	int i;

	sdev->ipc->msg.reply_error = status;
	if (status) {
		for (i = 0; i < sizeof(ipc4_status) / sizeof(struct sof_ipc4_fw_status); i++) {
			if (ipc4_status[i].status == status) {
				dev_err(sdev->dev, "FW reported error: %s", ipc4_status[i].msg);
				return;
			}
		}

		dev_err(sdev->dev, "FW reported unknown error, status = %d", status);
	}

	snd_sof_ipc_reply(sdev, msg);
}
EXPORT_SYMBOL(sof_ipc4_check_reply_status);

static void sof_ipc4_dump_config(struct snd_sof_dev *sdev,
				 void *config, u32 size)
{
	int *data = (int *)config;
	int i, dw_size;
	char buffer[36];

	dw_size = size >> 2;
	for (i = 0; i < dw_size; i += 4) {
		int len = (i + 4 < dw_size) ? 16 : (dw_size - i) * 4;

		hex_dump_to_buffer(data + i, len, 16, 4, buffer,
				   sizeof(buffer), false);
		dev_dbg(sdev->dev, "%s", buffer);
	}
}

/* wait for IPC message reply */
static int ipc4_tx_wait_done(struct snd_sof_ipc *ipc, struct snd_sof_ipc_msg *msg,
			void *reply_data)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	int ret;

	/* wait for DSP IPC completion */
	ret = wait_event_timeout(msg->waitq, msg->ipc_complete,
				 msecs_to_jiffies(sdev->ipc_timeout));
	if (ret == 0) {
		dev_err(sdev->dev, "error: ipc timed out for header:0x%x extension:0x%x",
			msg->header, msg->extension);
		return -ETIMEDOUT;
	} else if (msg->reply_error) {
		dev_err(sdev->dev, "error: ipc error for msg 0x%x : 0x%x\n",
			msg->header, msg->extension);
		return -EIO;
	}

	return 0;
}

static int sof_ipc4_tx_message_unlocked(struct snd_sof_ipc *ipc, u32 header,
				       u32 extension, void *msg_data, size_t msg_bytes,
				       void *reply_data, size_t reply_bytes)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg;
	int ret;

	if (ipc->disable_ipc_tx)
		return -ENODEV;

	/*
	 * The spin-lock is also still needed to protect message objects against
	 * other atomic contexts.
	 */
	spin_lock_irq(&sdev->ipc_lock);

	/* initialise the message */
	msg = &ipc->msg;

	msg->header = header;
	msg->extension = extension;
	msg->msg_size = msg_bytes;
	msg->reply_size = reply_bytes;
	msg->reply_error = 0;

	/* attach any data */
	if (msg_bytes > msg->msg_data_size) {
		devm_kfree(sdev->dev, msg->msg_data);
		msg->msg_data = devm_kzalloc(sdev->dev, msg_bytes, GFP_KERNEL);
		if (!msg->msg_data) {
			spin_unlock_irq(&sdev->ipc_lock);
			return -ENOMEM;
		}

		msg->msg_data_size = msg_bytes;
	}
	memcpy(msg->msg_data, msg_data, msg_bytes);

	sdev->msg = msg;

	ret = snd_sof_dsp_send_msg(sdev, msg);
	/* Next reply that we receive will be related to this message */
	if (!ret)
		msg->ipc_complete = false;

	spin_unlock_irq(&sdev->ipc_lock);

	if (ret < 0) {
		dev_err_ratelimited(sdev->dev, "error: ipc tx failed with error %d", ret);
		return ret;
	}

	/* now wait for completion */
	if (!ret)
		ret = ipc4_tx_wait_done(ipc, msg, reply_data);

	return ret;
}

int sof_ipc4_tx_message(struct snd_sof_ipc *ipc, u32 header, u32 extension,
		       void *msg_data, size_t msg_bytes, void *reply_data,
		       size_t reply_bytes)
{
	int ret;

	/* Serialise IPC TX */
	mutex_lock(&ipc->tx_mutex);

	ret = sof_ipc4_tx_message_unlocked(ipc, header, extension, msg_data, msg_bytes,
					  reply_data, reply_bytes);

	mutex_unlock(&ipc->tx_mutex);

	return ret;
}
EXPORT_SYMBOL(sof_ipc4_tx_message);

void snd_sof_ipc4_msgs_rx(struct snd_sof_dev *sdev, u32 msg, u32 msg_ext)
{
	int err;

	if (!SOF_IPC4_GLB_NOTIFY_MSG_TYPE(msg))
		return;

	switch (SOF_IPC4_GLB_NOTIFY_TYPE(msg)) {
	case SOF_IPC4_GLB_NOTIFY_FW_READY:
		/* check for FW boot completion */
		if (sdev->fw_state == SOF_FW_BOOT_IN_PROGRESS) {
			err = sof_ops(sdev)->fw_ready(sdev, msg);
			if (err < 0)
				sdev->fw_state = SOF_FW_BOOT_READY_FAILED;
			else
				sdev->fw_state = SOF_FW_BOOT_COMPLETE;

			/* wake up firmware loader */
			wake_up(&sdev->boot_wait);
		}

		break;
	default:
		break;
	}
}
EXPORT_SYMBOL(snd_sof_ipc4_msgs_rx);

int sof_ipc4_create_pipeline(struct snd_sof_dev *sdev, u32 id, u32 memory_size,
			int priority, int lp_mode)
{
	struct sof_ipc_reply reply;
	u32 header, extension;
	int ret;

	dev_dbg(sdev->dev, "ipc4 create pipeline %d", id);

	header = memory_size;
	header |= SOF_IPC4_GLB_PIPE_PRIORITY(priority);
	header |= SOF_IPC4_GLB_PIPE_INSTANCE_ID(id);
	header |= SOF_IPC4_GLB_MSG_TYPE(GLB_CREATE_PIPELINE);
	header |= SOF_IPC4_GLB_MSG_DIR(MSG_REQUEST);
	header |= SOF_IPC4_GLB_MSG_TARGET(FW_GEN_MSG);

	extension = lp_mode;

	ret = sof_ipc4_tx_message(sdev->ipc, header, extension, NULL, 0, &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to create pipeline %d", id);

	return 0;
}

int sof_ipc4_set_pipeline_status(struct snd_sof_dev *sdev, u32 id, u32 status)
{
	struct sof_ipc_reply reply;
	u32 header;
	int ret;

	dev_dbg(sdev->dev, "ipc4 set pipeline %d status %d", id, status);

	header = status;
	header |= SOF_IPC4_GL_PIPE_STATE_ID(id);
	header |= SOF_IPC4_GLB_MSG_TYPE(GLB_SET_PIPELINE_STATE);
	header |= SOF_IPC4_GLB_MSG_DIR(MSG_REQUEST);
	header |= SOF_IPC4_GLB_MSG_TARGET(FW_GEN_MSG);

	ret = sof_ipc4_tx_message(sdev->ipc, header, 0, NULL, 0, &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev,
			"error: failed to set pipeline %d status %d", id, status);

	return ret;
}

int sof_ipc4_delete_pipeline(struct snd_sof_dev *sdev, u32 id)
{
	struct sof_ipc_reply reply;
	u32 header;
	int ret;

	dev_dbg(sdev->dev, "ipc4 delete pipeline %d", id);

	header = SOF_IPC4_GLB_PIPE_INSTANCE_ID(id);
	header |= SOF_IPC4_GLB_MSG_TYPE(GLB_DELETE_PIPELINE);
	header |= SOF_IPC4_GLB_MSG_DIR(MSG_REQUEST);
	header |= SOF_IPC4_GLB_MSG_TARGET(FW_GEN_MSG);

	ret = sof_ipc4_tx_message(sdev->ipc, header, 0, NULL, 0, &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev,
			"error: failed to delete pipeline %d", id);

	return ret;
}

int sof_ipc4_initialize_module(struct snd_sof_dev *sdev, u32 mod_id, u32 instance_id,
				u32 param_size, u32 pipe_id, u32 core, u32 domain, void *data)
{
	struct sof_ipc_reply reply;
	u32 header, extension;
	int ret;

	dev_dbg(sdev->dev, "ipc4 create module %d - %d", mod_id, instance_id);

	header = mod_id;
	header |= SOF_IPC4_MOD_INSTANCE(instance_id);
	header |= SOF_IPC4_GLB_MSG_TYPE(MOD_INIT_INSTANCE);
	header |= SOF_IPC4_GLB_MSG_DIR(MSG_REQUEST);
	header |= SOF_IPC4_GLB_MSG_TARGET(MODULE_MSG);

	/* dword size */
	extension = param_size >> 2;
	extension |= SOF_IPC4_MOD_EXT_PPL_ID(pipe_id);
	extension |= SOF_IPC4_MOD_EXT_CORE_ID(core);
	extension |= SOF_IPC4_MOD_EXT_DOMAIN(domain);

	ret = sof_ipc4_tx_message(sdev->ipc, header, extension, data, param_size,
				 &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to create module %s : %d -%d",
			sdev->fw_modules[mod_id].name, mod_id, instance_id);

	sof_ipc4_dump_config(sdev, data, param_size);

	return ret;
}

int sof_ipc4_bind_modules(struct snd_sof_dev *sdev, u32 src_mod, u32 src_instance,
			u32 src_queue, u32 dst_mod, u32 dst_instance, u32 dst_queue)
{
	struct sof_ipc_reply reply;
	u32 header, extension;
	int ret;

	dev_dbg(sdev->dev, "ipc4 bind module %d -%d to module %d -%d",
		src_mod, src_instance, dst_mod, dst_instance);

	header = src_mod;
	header |= SOF_IPC4_MOD_INSTANCE(src_instance);
	header |= SOF_IPC4_GLB_MSG_TYPE(MOD_BIND);
	header |= SOF_IPC4_GLB_MSG_DIR(MSG_REQUEST);
	header |= SOF_IPC4_GLB_MSG_TARGET(MODULE_MSG);

	extension = dst_mod;
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(dst_instance);
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(dst_queue);
	extension |= SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(src_queue);

	ret = sof_ipc4_tx_message(sdev->ipc, header, extension, NULL, 0, &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to bind module %s: %d - %d to module %s: %d - %d",
			sdev->fw_modules[src_mod].name, src_mod, src_instance,
			sdev->fw_modules[dst_mod].name, dst_mod, dst_instance);

	return ret;
}

int sof_ipc4_unbind_modules(struct snd_sof_dev *sdev, u32 src_mod, u32 src_instance,
			u32 src_queue, u32 dst_mod, u32 dst_instance, u32 dst_queue)
{
	struct sof_ipc_reply reply;
	u32 header, extension;
	int ret;

	dev_dbg(sdev->dev, "ipc4 unbind module %d -%d to module %d -%d",
			src_mod, src_instance, dst_mod, dst_instance);

	header = src_mod;
	header |= SOF_IPC4_MOD_INSTANCE(src_instance);
	header |= SOF_IPC4_GLB_MSG_TYPE(MOD_UNBIND);
	header |= SOF_IPC4_GLB_MSG_DIR(MSG_REQUEST);
	header |= SOF_IPC4_GLB_MSG_TARGET(MODULE_MSG);

	extension = dst_mod;
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_INSTANCE(dst_instance);
	extension |= SOF_IPC4_MOD_EXT_DST_MOD_QUEUE_ID(dst_queue);
	extension |= SOF_IPC4_MOD_EXT_SRC_MOD_QUEUE_ID(src_queue);

	ret = sof_ipc4_tx_message(sdev->ipc, header, extension, NULL, 0, &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to unbind module %s: %d - %d to module %s: %d - %d",
			sdev->fw_modules[src_mod].name, src_mod, src_instance,
			sdev->fw_modules[dst_mod].name, dst_mod, dst_instance);

	return ret;
}

int sof_ipc4_set_large_config_module(struct snd_sof_dev *sdev, u32 mod_id, u32 instance_id,
					u32 data_size, u32 param_id, void *data)
{
	struct sof_ipc_reply reply;
	u32 header, extension;
	int send_size = 0;
	int ret;

	dev_dbg(sdev->dev, "set large config of module %d - %d", mod_id, instance_id);

	header = mod_id;
	header |= SOF_IPC4_MOD_INSTANCE(instance_id);
	header |= SOF_IPC4_GLB_MSG_TYPE(MOD_LARGE_CONFIG_SET);
	header |= SOF_IPC4_GLB_MSG_DIR(MSG_REQUEST);
	header |= SOF_IPC4_GLB_MSG_TARGET(MODULE_MSG);

	extension = SOF_IPC4_MOD_EXT_MSG_PARAM_ID(param_id);
	extension |= SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK(0);
	extension |= SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK(1);

	do {
		int block_size;

		block_size = data_size;
		if (data_size > sdev->host_box.size) {
			block_size = sdev->host_box.size;
			data_size -= sdev->host_box.size;
		} else {
			extension |= SOF_IPC4_MOD_EXT_MSG_LAST_BLOCK(1);
			data_size -= block_size;
		}

		extension &= ~SOF_IPC4_MOD_EXT_MSG_SIZE_MASK;
		extension |= SOF_IPC4_MOD_EXT_MSG_SIZE(block_size);
		ret = sof_ipc4_tx_message(sdev->ipc, header, extension,
					  data + send_size, block_size,
					  &reply, sizeof(reply));
		if (ret < 0) {
			dev_err(sdev->dev, "error: failed to large config of module %s: %d - %d",
				sdev->fw_modules[mod_id].name, mod_id, instance_id);
			break;
		}

		extension &= ~SOF_IPC4_MOD_EXT_MSG_FIRST_BLOCK_MASK;
		send_size += block_size;
	} while (data_size);

	return ret;
}

int sof_ipc4_delete_module(struct snd_sof_dev *sdev, u32 mod_id, u32 instance_id)
{
	struct sof_ipc_reply reply;
	u32 header;
	int ret;

	dev_dbg(sdev->dev, "ipc4 delete module %d -%d", mod_id, instance_id);

	header = mod_id;
	header |= SOF_IPC4_MOD_INSTANCE(instance_id);
	header |= SOF_IPC4_GLB_MSG_TYPE(MOD_DELETE_INSTANCE);
	header |= SOF_IPC4_GLB_MSG_DIR(MSG_REQUEST);
	header |= SOF_IPC4_GLB_MSG_TARGET(MODULE_MSG);

	ret = sof_ipc4_tx_message(sdev->ipc, header, 0, NULL, 0, &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to delete module %s: %d -%d",
			sdev->fw_modules[mod_id].name, mod_id, instance_id);

	return ret;
}
