/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 *
 * Author: Rander Wang <rander.wang@linux.intel.com>
 */

#ifndef __SOF_IPC4_I2S_H
#define __SOF_IPC4_I2S_H

#include "ipc4-intel.h"

#define SSP_SET_BIT(b, x)		(((x) & 1) << (b))
#define SSP_SET_BITS(b_hi, b_lo, x)	\
	(((x) & ((1ULL << ((b_hi) - (b_lo) + 1ULL)) - 1ULL)) << (b_lo))

/* SSCR0 bits */
#define SSP_SSCR0_DSIZE(x)	SSP_SET_BITS(3, 0, (x) - 1)
#define SSP_SSCR0_FRF	MASK(5, 4)
#define SSP_SSCR0_MOT	SSP_SET_BITS(5, 4, 0)
#define SSP_SSCR0_TI	SSP_SET_BITS(5, 4, 1)
#define SSP_SSCR0_NAT	SSP_SET_BITS(5, 4, 2)
#define SSP_SSCR0_PSP	SSP_SET_BITS(5, 4, 3)
#define SSP_SSCR0_ECS	BIT(6)
#define SSP_SSCR0_SSE	BIT(7)
#define SSP_SSCR0_SCR_MASK	MASK(19, 8)
#define SSP_SSCR0_SCR(x)	SSP_SET_BITS(19, 8, x)
#define SSP_SSCR0_EDSS	BIT(20)
#define SSP_SSCR0_NCS	BIT(21)
#define SSP_SSCR0_RIM	BIT(22)
#define SSP_SSCR0_TIM	BIT(23)
#define SSP_SSCR0_FRDC(x)	SSP_SET_BITS(26, 24, (x) - 1)
#define SSP_SSCR0_ACS	BIT(30)
#define SSP_SSCR0_MOD	BIT(31)

/* SSCR1 bits */
#define SSP_SSCR1_RIE	BIT(0)
#define SSP_SSCR1_TIE	BIT(1)
#define SSP_SSCR1_LBM	BIT(2)
#define SSP_SSCR1_SPO	BIT(3)
#define SSP_SSCR1_SPH	BIT(4)
#define SSP_SSCR1_MWDS	BIT(5)
#define SSP_SSCR1_TFT_MASK	MASK(9, 6)
#define SSP_SSCR1_TFT(x)	SSP_SET_BITS(9, 6, (x) - 1)
#define SSP_SSCR1_RFT_MASK	MASK(13, 10)
#define SSP_SSCR1_RFT(x)	SSP_SET_BITS(13, 10, (x) - 1)
#define SSP_SSCR1_EFWR	BIT(14)
#define SSP_SSCR1_STRF	BIT(15)
#define SSP_SSCR1_IFS	BIT(16)
#define SSP_SSCR1_PINTE	BIT(18)
#define SSP_SSCR1_TINTE	BIT(19)
#define SSP_SSCR1_RSRE	BIT(20)
#define SSP_SSCR1_TSRE	BIT(21)
#define SSP_SSCR1_TRAIL	BIT(22)
#define SSP_SSCR1_RWOT	BIT(23)
#define SSP_SSCR1_SFRMDIR	BIT(24)
#define SSP_SSCR1_SCLKDIR	BIT(25)
#define SSP_SSCR1_ECRB	BIT(26)
#define SSP_SSCR1_ECRA	BIT(27)
#define SSP_SSCR1_SCFR	BIT(28)
#define SSP_SSCR1_EBCEI	BIT(29)
#define SSP_SSCR1_TTE	BIT(30)
#define SSP_SSCR1_TTELP	BIT(31)

#define SSP_SSCR2_TURM1		BIT(1)
#define SSP_SSCR2_PSPSRWFDFD	BIT(3)
#define SSP_SSCR2_PSPSTWFDFD	BIT(4)
#define SSP_SSCR2_SDFD		BIT(14)
#define SSP_SSCR2_SDPM		BIT(16)
#define SSP_SSCR2_LJDFD		BIT(17)
#define SSP_SSCR2_MMRATF		BIT(18)
#define SSP_SSCR2_SMTATF		BIT(19)

/* SSR bits */
#define SSP_SSSR_TNF	BIT(2)
#define SSP_SSSR_RNE	BIT(3)
#define SSP_SSSR_BSY	BIT(4)
#define SSP_SSSR_TFS	BIT(5)
#define SSP_SSSR_RFS	BIT(6)
#define SSP_SSSR_ROR	BIT(7)
#define SSP_SSSR_TUR	BIT(21)

/* SSPSP bits */
#define SSP_SSPSP_SCMODE(x)		SSP_SET_BITS(1, 0, x)
#define SSP_SSPSP_SFRMP(x)		SSP_SET_BITS(2, x)
#define SSP_SSPSP_ETDS		BIT(3)
#define SSP_SSPSP_STRTDLY(x)	SSP_SET_BITS(6, 4, x)
#define SSP_SSPSP_DMYSTRT(x)	SSP_SET_BITS(8, 7, x)
#define SSP_SSPSP_SFRMDLY(x)	SSP_SET_BITS(15, 9, x)
#define SSP_SSPSP_SFRMWDTH(x)	SSP_SET_BITS(21, 16, x)
#define SSP_SSPSP_DMYSTOP(x)	SSP_SET_BITS(24, 23, x)
#define SSP_SSPSP_DMYSTOP_BITS	2
#define SSP_SSPSP_DMYSTOP_MASK MASK(SSPSP_DMYSTOP_BITS - 1, 0)
#define SSP_SSPSP_FSRT		BIT(25)
#define SSP_SSPSP_EDMYSTOP(x)	SSP_SET_BITS(28, 26, x)

#define SSP_SSPSP2			0x44
#define SSP_SSPSP2_FEP_MASK		0xff

#define SSP_SSCR3		0x48
#define SSP_SSIOC		0x4C

#define SSP_SSP_REG_MAX	SSIOC

/* SSTSA bits */
#define SSP_SSTSA_SSTSA(x)		SSP_SET_BITS(7, 0, x)
#define SSP_SSTSA_TXEN		BIT(8)

/* SSRSA bits */
#define SSP_SSRSA_SSRSA(x)		SSP_SET_BITS(7, 0, x)
#define SSP_SSRSA_RXEN		BIT(8)

/* SSCR3 bits */
#define SSP_SSCR3_FRM_MST_EN	BIT(0)
#define SSP_SSCR3_I2S_MODE_EN	BIT(1)
#define SSP_SSCR3_I2S_FRM_POL(x)	SSP_SET_BITS(2, x)
#define SSP_SSCR3_I2S_TX_SS_FIX_EN	BIT(3)
#define SSP_SSCR3_I2S_RX_SS_FIX_EN	BIT(4)
#define SSP_SSCR3_I2S_TX_EN		BIT(9)
#define SSP_SSCR3_I2S_RX_EN		BIT(10)
#define SSP_SSCR3_CLK_EDGE_SEL	BIT(12)
#define SSP_SSCR3_STRETCH_TX	BIT(14)
#define SSP_SSCR3_STRETCH_RX	BIT(15)
#define SSP_SSCR3_MST_CLK_EN	BIT(16)
#define SSP_SSCR3_SYN_FIX_EN	BIT(17)

/* SFIFOTT bits */
#define SSP_SFIFOTT_TX(x)		((x) - 1)
#define SSP_SFIFOTT_RX(x)		(((x) - 1) << 16)

/* SFIFOL bits */
#define SSP_SFIFOL_TFL(x)		((x) & 0xFFFF)
#define SSP_SFIFOL_RFL(x)		((x) >> 16)

#define SSP_SSTSA_TSEN			BIT(8)
#define SSP_SSRSA_RSEN			BIT(8)

#define SSP_SSCR3_TFL_MASK	MASK(5, 0)
#define SSP_SSCR3_RFL_MASK	MASK(13, 8)
#define SSP_SSCR3_TFL_VAL(scr3_val)	(((scr3_val) >> 0) & MASK(5, 0))
#define SSP_SSCR3_RFL_VAL(scr3_val)	(((scr3_val) >> 8) & MASK(5, 0))
#define SSP_SSCR3_TX(x)	SSP_SET_BITS(21, 16, (x) - 1)
#define SSP_SSCR3_RX(x)	SSP_SET_BITS(29, 24, (x) - 1)

#define SSP_SSIOC_TXDPDEB	BIT(1)
#define SSP_SSIOC_SFCR	BIT(4)
#define SSP_SSIOC_SCOE	BIT(5)

#define I2S_TDM_INVALID_SLOT_MAP1 0xF
#define I2S_TDM_MAX_CHANNEL_COUNT 8
#define I2S_TDM_MAX_SLOT_MAP_COUNT 8

/* i2s Configuration BLOB building blocks */

/* i2s registers for i2s Configuration */
struct sof_i2s_config {
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
};

struct sof_mclk_config {
	/* master clock divider control register */
	uint32_t mdivc;

	/* master clock divider ratio register */
	uint32_t mdivr;
};

struct sof_i2s_driver_config {
	struct sof_i2s_config i2s_config;
	struct sof_mclk_config mclk_config;
};

struct sof_i2s_start_control {
	/*
	 * delay in msec between enabling interface (moment when Copier
	 * instance is being attached to the interface) and actual
	 * interface start. Value of 0 means no delay.
	 */
	uint32_t clock_warm_up    : 16;

	/* specifies if parameters target MCLK (1) or SCLK (0) */
	uint32_t mclk             : 1;

	/*
	 * value of 1 means that clock should be started immediately
	 * even if no Copier instance is currently attached to the interface.
	 */
	uint32_t warm_up_ovr      : 1;
	uint32_t rsvd0            : 14;
};

struct sof_i2s_stop_control {
	/*
	 * delay in msec between stopping the interface
	 * (moment when Copier instance is being detached from the interface)
	 * and interface clock stop. Value of 0 means no delay.
	 */
	uint32_t clock_stop_delay : 16;

	/*
	 * value of 1 means that clock should be kept running (infinite
	 * stop delay) after Copier instance detaches from the interface.
	 */
	uint32_t keep_running     : 1;

	/*
	 * value of 1 means that clock should be stopped immediately.
	 */
	uint32_t clock_stop_ovr   : 1;
	uint32_t rsvd1            : 14;
};

union sof_i2s_dma_control {
	struct ControlData {
		struct sof_i2s_start_control start_control;
		struct sof_i2s_stop_control stop_control;
	} control_data;

	struct MnDivControlData {
		uint32_t mval;
		uint32_t nval;
	} mndiv_control_data;
};

struct sof_i2s_configuration_blob {
	struct sof_gtw_attributes gw_attr;

	/* TDM time slot mappings */
	uint32_t tdm_ts_group[I2S_TDM_MAX_SLOT_MAP_COUNT];

	/* i2s port configuration */
	struct sof_i2s_driver_config i2s_driver_config;

	/* optional configuration parameters */
	union sof_i2s_dma_control i2s_dma_control[0];
};

int sof_ipc4_generate_ssp_blob(struct snd_sof_dev *sdev,
				struct sof_ipc4_dai *ipc4_dai,
				int lp_mode);

#endif //__SOF_IPC4_I2S_H
