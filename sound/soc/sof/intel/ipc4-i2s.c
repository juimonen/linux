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
#include "ipc4-i2s.h"
#include "shim.h"

static int sof_ipc4_generate_ssp_config(struct snd_sof_dev *sdev, struct sof_ipc4_dai *ipc4_dai,
				struct sof_i2s_configuration_blob *blob)
{
	const struct sof_dev_desc *desc = sdev->pdata->desc;
	const struct sof_intel_dsp_desc *chip_info;
	struct sof_ipc_dai_ssp_params *ssp;
	uint32_t clk_div, end_padding, total_sample_size;
	uint32_t ssc0;
	uint32_t ssc1;
	uint32_t sscto;
	uint32_t sspsp;
	uint32_t sstsa;
	uint32_t ssrsa;
	uint32_t ssc2;
	uint32_t sspsp2;
	uint32_t ssc3;
	uint32_t ssioc;

	chip_info = (const struct sof_intel_dsp_desc *)desc->chip_info;

	ssp = &ipc4_dai->dai.dai_config->ssp;

	dev_dbg(sdev->dev, "tdm_slot_width %d, tdm_slots %d, mclk %d",
		ssp->tdm_slot_width, ssp->tdm_slots, ssp->mclk_rate);

	if (ssp->tdm_slot_width != 32) {
		dev_warn(sdev->dev, "error: tdm slot should be 32bit for fw");
		ssp->tdm_slot_width = 32;
	}

	/* ssc0 settings */
	ssc0 = SSP_SSCR0_MOD;
	ssc0 |= SSP_SSCR0_FRDC(ssp->tdm_slots);
	ssc0 |= SSP_SSCR0_TIM;
	ssc0 |= SSP_SSCR0_RIM;

	if (ssp->tdm_slot_width > 16) {
		ssc0 |= SSP_SSCR0_EDSS;
		ssc0 |= SSP_SSCR0_DSIZE(ssp->tdm_slot_width - 16);
	} else
		ssc0 |= SSP_SSCR0_DSIZE(ssp->tdm_slot_width);

	ssc0 |= SSP_SSCR0_PSP;

	/*
	 * sclk is generated based on scr setting, not deprecated m&n divider.
	 * Now only tdm mode is supported and the frame size = ssp->tdm_slot_width *
	 * ssp->tdm_slots + end_padding. Here try to adjust end_padding to make mclk
	 * is exact multiple size of frame size to utilize clock division feature with scr
	 * For master clock 38.4MHZ, 2ch, 32bit sample container size, rate 48k
	 * 38.4M = (2 * 32 + 16) * 48k * 10 (scr value).
	 * For 4 ch case,
	 * 38.4M = (4 * 32 + 72) * 48k * 4 (scr)
	 * For 8 ch case,
	 * 38.4M = (8 * 32 + 144) * 48k * 2 (scr)
	 */
	end_padding = 0;
	total_sample_size = ssp->tdm_slot_width * ssp->tdm_slots;
	while (chip_info->ssp_mclk % ((total_sample_size + end_padding) * ssp->fsync_rate)) {
		if (++end_padding >= 256)
			break;
	}

	if (end_padding >= 256)
		return -EINVAL;

	clk_div = chip_info->ssp_mclk / ((total_sample_size + end_padding) * ssp->fsync_rate);
	if (clk_div >= 4095)
		return -EINVAL;
	ssc0 |= SSP_SSCR0_SCR(clk_div - 1);

	blob->i2s_driver_config.i2s_config.ssc0 = ssc0;

	/* ssc1 settings */
	ssc1 = SSP_SSCR1_TTELP | SSP_SSCR1_TTE;
	/* clock is stopped in inactive statue */
	ssc1 |= SSP_SSCR1_SCFR;

	/* master or slave mode */
	if (!ssp->mclk_direction) {
		ssc1 |= SSP_SSCR1_SCLKDIR;
		ssc1 |= SSP_SSCR1_SFRMDIR;
	}

	ssc1 |= SSP_SSCR1_TRAIL | SSP_SSCR1_RSRE | SSP_SSCR1_TSRE;

	/* Receiver Time-out Interrupt Disabled/Enabled */
	ssc1 |= (ssp->quirks & SOF_DAI_INTEL_SSP_QUIRK_TINTE) ?
		SSP_SSCR1_TINTE : 0;

	/* Peripheral Trailing Byte Interrupts Disable/Enable */
	ssc1 |= (ssp->quirks & SOF_DAI_INTEL_SSP_QUIRK_PINTE) ?
		SSP_SSCR1_PINTE : 0;

	/*
	 * Enable/disable internal loopback. Output of transmit serial
	 * shifter connected to input of receive serial shifter, internally.
	 */
	ssc1 |= (ssp->quirks & SOF_DAI_INTEL_SSP_QUIRK_LBM) ?
		SSP_SSCR1_LBM : 0;

	blob->i2s_driver_config.i2s_config.ssc1 = ssc1;

	/* sscto settings */
	sscto = 0;
	blob->i2s_driver_config.i2s_config.sscto = sscto;

	/* sspsp settings */
	/* default in TDM mode */
	sspsp = SSP_SSPSP_SFRMWDTH(1);
	blob->i2s_driver_config.i2s_config.sspsp = sspsp;

	/* sstsa & ssrsa settings */
	sstsa |= SSP_SSTSA_SSTSA(ssp->tx_slots);
	ssrsa |= SSP_SSRSA_SSRSA(ssp->rx_slots);
	blob->i2s_driver_config.i2s_config.sstsa = sstsa;
	blob->i2s_driver_config.i2s_config.ssrsa = ssrsa;

	/* ssc2 settings */
	ssc2 = SSP_SSCR2_SDFD | SSP_SSCR2_TURM1;

	/*
	 * Transmit data are driven at the same/opposite clock edge
	 * specified in SSPSP.SCMODE[1:0]
	 */
	ssc2 |= (ssp->quirks & SOF_DAI_INTEL_SSP_QUIRK_SMTATF) ?
		SSP_SSCR2_SMTATF : 0;

	/*
	 * Receive data are sampled at the same/opposite clock edge specified
	 * in SSPSP.SCMODE[1:0]
	 */
	ssc2 |= (ssp->quirks & SOF_DAI_INTEL_SSP_QUIRK_MMRATF) ?
		SSP_SSCR2_MMRATF : 0;

	/*
	 * Enable/disable the fix for PSP consumer mode TXD wait for frame
	 * de-assertion before starting the second channel
	 */
	ssc2 |= (ssp->quirks & SOF_DAI_INTEL_SSP_QUIRK_PSPSTWFDFD) ?
		SSP_SSCR2_PSPSTWFDFD : 0;

	/*
	 * Enable/disable the fix for PSP provider mode FSRT with dummy stop &
	 * frame end padding capability
	 */
	ssc2 |= (ssp->quirks & SOF_DAI_INTEL_SSP_QUIRK_PSPSRWFDFD) ?
		SSP_SSCR2_PSPSRWFDFD : 0;

	blob->i2s_driver_config.i2s_config.ssc2 = ssc2;

	/* sspsp2 settings */
	sspsp2 = end_padding;
	blob->i2s_driver_config.i2s_config.sspsp2 = sspsp2;

	ssc3 = SSP_SSCR3_TX(8) | SSP_SSCR3_RX(8);
	blob->i2s_driver_config.i2s_config.ssc3 = ssc3;

	if (!ssp->mclk_direction)
		ssioc = SSP_SSIOC_SCOE;
	blob->i2s_driver_config.i2s_config.ssioc = ssioc;


	/* generate mclk for codec */
	if (!ssp->mclk_rate) {
		dev_info(sdev->dev, "mclk is not provided to codec");
		return 0;
	}

	clk_div = chip_info->ssp_mclk / ssp->mclk_rate;
	if (clk_div > 1)
		clk_div -= 2;
	else
		clk_div = 0xFFF; /* bypass clk divider */

	/* use clock source 0 */
	blob->i2s_driver_config.mclk_config.mdivc = BIT(0);
	blob->i2s_driver_config.mclk_config.mdivr = clk_div;

	return 0;
}

int sof_ipc4_generate_ssp_blob(struct snd_sof_dev *sdev, struct sof_ipc4_dai *ipc4_dai, int lp_mode)
{
	struct sof_i2s_configuration_blob *blob;
	struct sof_gtw_attributes gtw_attr;
	int channels, i;
	int ret;

	blob = devm_kzalloc(sdev->dev, sizeof(*blob), GFP_KERNEL);
	if (!blob)
		return -ENOMEM;

	gtw_attr.lp_buffer_alloc = lp_mode;

	blob->gw_attr = gtw_attr;

	channels = ipc4_dai->dai.dai_config->ssp.tdm_slots;
	for (i = 0; i < channels; i++)
		blob->tdm_ts_group[0] |= (i << (i * 4));
	for (; i < I2S_TDM_MAX_CHANNEL_COUNT; i++)
		blob->tdm_ts_group[0] |= (0xF << (i * 4));

	ret = sof_ipc4_generate_ssp_config(sdev, ipc4_dai, blob);
	if (ret) {
		dev_err(sdev->dev, "failed to generate ssp config for dai %s", ipc4_dai->dai.name);
		return -EINVAL;
	}

	ipc4_dai->copier.gtw_cfg.config_length = sizeof(*blob) >> 2;
	ipc4_dai->copier_config = (uint32_t *)blob;

	return 0;
}
