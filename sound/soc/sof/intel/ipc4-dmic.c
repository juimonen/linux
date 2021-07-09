// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
// Authors: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>
// Authors: Rander Wang <rander.wang@linux.intel.com>

/* Intel dmic code */

/* Global configuration request for DMIC */
#include <sound/pcm_params.h>
#include "../sof-audio.h"
#include "../ipc4-topology.h"
#include "../ipc4.h"
#include "ipc4-i2s.h"
#include "shim.h"
#include "ipc4-dmic.h"
#include "pdm_decim/pdm_decim_table.h"

static struct sof_ipc_dai_dmic_params *dmic_prm[DMIC_HW_FIFOS];

/* This function searches from vec[] (of length vec_length) integer values
 * of n. The indexes to equal values is returned in idx[]. The function
 * returns the number of found matches. The max_results should be set to
 * 0 (or negative) or vec_length get all the matches. The max_result can be set
 * to 1 to receive only the first match in ascending order. It avoids need
 * for an array for idx.
 */
int find_equal_int16(int16_t idx[], int16_t vec[], int n, int vec_length,
	int max_results)
{
	int nresults = 0;
	int i;

	for (i = 0; i < vec_length; i++) {
		if (vec[i] == n) {
			idx[nresults++] = i;
			if (nresults == max_results)
				break;
		}
	}

	return nresults;
}

static inline int ceil_divide(int a, int b)
{
	int c;

	c = a / b;

	/* First, we check whether the signs of the params are different.
	 * If they are, we already know the result is going to be negative and
	 * therefore, is going to be already rounded up (truncated).
	 *
	 * If the signs are the same, we check if there was any remainder in
	 * the division by multiplying the number back.
	 */
	if (!((a ^ b) & (1 << ((sizeof(int) * 8) - 1))) && c * b != a)
		c++;

	return c;
}

/* This function returns a raw list of potential microphone clock and decimation
 * modes for achieving requested sample rates. The search is constrained by
 * decimation HW capabililies and setup parameters. The parameters such as
 * microphone clock min/DMIC_MAX and duty cycle requirements need be checked from
 * used microphone component datasheet.
 */
static void find_modes(struct snd_sof_dev *sdev, struct decim_modes *modes, uint32_t fs, int di)
{
	const struct sof_dev_desc *desc = sdev->pdata->desc;
	const struct sof_intel_dsp_desc *chip_info;
	int clkdiv_min;
	int clkdiv_max;
	int clkdiv;
	int c1;
	int du_min;
	int du_max;
	int pdmclk;
	int osr;
	int mfir;
	int mcic;
	int ioclk_test;
	int osr_min = DMIC_MIN_OSR;
	int j;
	int i = 0;

	/* Defaults, empty result */
	modes->num_of_modes = 0;

	/* The FIFO is not requested if sample rate is set to zero. Just
	 * return in such case with num_of_modes as zero.
	 */
	if (fs == 0)
		return;

	/* Override DMIC_MIN_OSR for very high sample rates, use as minimum
	 * the nominal clock for the high rates.
	 */
	if (fs >= DMIC_HIGH_RATE_MIN_FS)
		osr_min = DMIC_HIGH_RATE_OSR_MIN;

	chip_info = desc->chip_info;
	/* Check for sane pdm clock, min 100 kHz, DMIC_MAX ioclk/2 */
	if (dmic_prm[di]->pdmclk_max < DMIC_HW_PDM_CLK_MIN ||
	    dmic_prm[di]->pdmclk_max > chip_info->dmic_mclk / 2) {
		dev_err(sdev->dev, "%s: pdm clock DMIC_MAX not in range", __func__);
		return;
	}
	if (dmic_prm[di]->pdmclk_min < DMIC_HW_PDM_CLK_MIN ||
	    dmic_prm[di]->pdmclk_min > dmic_prm[di]->pdmclk_max) {
		dev_err(sdev->dev, "%s: pdm clock min not in range", __func__);
		return;
	}

	/* Check for sane duty cycle */
	if (dmic_prm[di]->duty_min > DMIC_HW_DUTY_MAX) {
		dev_err(sdev->dev, "%s: duty cycle min > max", __func__);
		return;
	}
	if (dmic_prm[di]->duty_min < DMIC_HW_DUTY_MIN ||
	    dmic_prm[di]->duty_min > DMIC_HW_DUTY_MAX) {
		dev_err(sdev->dev, "%s: pdm clock min not in range", __func__);
		return;
	}
	if (dmic_prm[di]->duty_max < DMIC_HW_DUTY_MIN ||
	    dmic_prm[di]->duty_max > DMIC_HW_DUTY_MAX) {
		dev_err(sdev->dev, "%s: pdm clock max not in range", __func__);
		return;
	}

	/* Min and Max clock dividers */
	clkdiv_min = ceil_divide(chip_info->dmic_mclk, dmic_prm[di]->pdmclk_max);
	clkdiv_min = DMIC_MAX(clkdiv_min, DMIC_HW_CIC_DECIM_MIN);
	clkdiv_max = chip_info->dmic_mclk / dmic_prm[di]->pdmclk_min;

	/* Loop possible clock dividers and check based on resulting
	 * oversampling ratio that CIC and FIR decimation ratios are
	 * feasible. The ratios need to be integers. Also the mic clock
	 * duty cycle need to be within limits.
	 */
	for (clkdiv = clkdiv_min; clkdiv <= clkdiv_max; clkdiv++) {
		/* Calculate duty cycle for this clock divider. Note that
		 * odd dividers cause non-50% duty cycle.
		 */
		c1 = clkdiv >> 1;
		du_min = 100 * c1 / clkdiv;
		du_max = 100 - du_min;

		/* Calculate PDM clock rate and oversampling ratio. */
		pdmclk = chip_info->dmic_mclk / clkdiv;
		osr = pdmclk / fs;

		/* Check that OSR constraints is met and clock duty cycle does
		 * not exceed microphone specification. If exceed proceed to
		 * next clkdiv.
		 */
		if (osr < osr_min || du_min < dmic_prm[di]->duty_min ||
		    du_max > dmic_prm[di]->duty_max)
			continue;

		/* Loop FIR decimation factors candidates. If the
		 * integer divided decimation factors and clock dividers
		 * as multiplied with sample rate match the IO clock
		 * rate the division was exact and such decimation mode
		 * is possible. Then check that CIC decimation constraints
		 * are met. The passed decimation modes are added to array.
		 */
		for (j = 0; fir_list[j]; j++) {
			mfir = fir_list[j]->decim_factor;

			/* Skip if previous decimation factor was the same */
			if (j > 1 && fir_list[j - 1]->decim_factor == mfir)
				continue;

			mcic = osr / mfir;
			ioclk_test = fs * mfir * mcic * clkdiv;

			if (ioclk_test == chip_info->dmic_mclk &&
			    mcic >= DMIC_HW_CIC_DECIM_MIN &&
			    mcic <= DMIC_HW_CIC_DECIM_MAX &&
			    i < DMIC_MAX_MODES) {
				modes->clkdiv[i] = clkdiv;
				modes->mcic[i] = mcic;
				modes->mfir[i] = mfir;
				i++;
			}
		}
	}

	modes->num_of_modes = i;
}

/* The previous raw modes list contains sane configuration possibilities. When
 * there is request for both FIFOs A and B operation this function returns
 * list of compatible settings.
 */
static void match_modes(struct matched_modes *c, struct decim_modes *a,
			struct decim_modes *b)
{
	int16_t idx[DMIC_MAX_MODES];
	int idx_length;
	int i;
	int n;
	int m;

	/* Check if previous search got results. */
	c->num_of_modes = 0;
	if (a->num_of_modes == 0 && b->num_of_modes == 0) {
		/* Nothing to do */
		return;
	}

	/* Ensure that num_of_modes is sane. */
	if (a->num_of_modes > DMIC_MAX_MODES ||
	    b->num_of_modes > DMIC_MAX_MODES)
		return;

	/* Check for request only for FIFO A or B. In such case pass list for
	 * A or B as such.
	 */
	if (b->num_of_modes == 0) {
		c->num_of_modes = a->num_of_modes;
		for (i = 0; i < a->num_of_modes; i++) {
			c->clkdiv[i] = a->clkdiv[i];
			c->mcic[i] = a->mcic[i];
			c->mfir_a[i] = a->mfir[i];
			c->mfir_b[i] = 0; /* Mark FIR B as non-used */
		}
		return;
	}

	if (a->num_of_modes == 0) {
		c->num_of_modes = b->num_of_modes;
		for (i = 0; i < b->num_of_modes; i++) {
			c->clkdiv[i] = b->clkdiv[i];
			c->mcic[i] = b->mcic[i];
			c->mfir_b[i] = b->mfir[i];
			c->mfir_a[i] = 0; /* Mark FIR A as non-used */
		}
		return;
	}

	/* Merge a list of compatible modes */
	i = 0;
	for (n = 0; n < a->num_of_modes; n++) {
		/* Find all indices of values a->clkdiv[n] in b->clkdiv[] */
		idx_length = find_equal_int16(idx, b->clkdiv, a->clkdiv[n],
					      b->num_of_modes, 0);
		for (m = 0; m < idx_length; m++) {
			if (b->mcic[idx[m]] == a->mcic[n]) {
				c->clkdiv[i] = a->clkdiv[n];
				c->mcic[i] = a->mcic[n];
				c->mfir_a[i] = a->mfir[n];
				c->mfir_b[i] = b->mfir[idx[m]];
				i++;
			}
		}
		c->num_of_modes = i;
	}
}

/* Finds a suitable FIR decimation filter from the included set */
static struct pdm_decim *get_fir(struct snd_sof_dev *sdev, struct dmic_configuration *cfg, int mfir)
{
	const struct sof_dev_desc *desc = sdev->pdata->desc;
	const struct sof_intel_dsp_desc *chip_info;
	struct pdm_decim *fir = NULL;
	int fir_max_length;
	int cic_fs;
	int fs;
	int i;

	if (mfir <= 0)
		return fir;

	chip_info = desc->chip_info;
	cic_fs = chip_info->dmic_mclk / cfg->clkdiv / cfg->mcic;
	fs = cic_fs / mfir;
	/* FIR DMIC_MAX. length depends on available cycles and coef RAM
	 * length. Exceeding this length sets HW overrun status and
	 * overwrite of other register.
	 */
	fir_max_length = DMIC_MIN(DMIC_HW_FIR_LENGTH_MAX,
			     chip_info->dmic_mclk / fs / 2 -
			     DMIC_FIR_PIPELINE_OVERHEAD);

	i = 0;
	/* Loop until NULL */
	while (fir_list[i]) {
		if (fir_list[i]->decim_factor == mfir) {
			if (fir_list[i]->length <= fir_max_length) {
				/* Store pointer, break from loop to avoid a
				 * Possible other mode with lower FIR length.
				 */
				fir = fir_list[i];
				break;
			}

			dev_dbg(sdev->dev, "%s: Note length=%d exceeds DMIC_MAX=%d",
				__func__, fir_list[i]->length, fir_max_length);
		}
		i++;
	}

	return fir;
}

/* Return the largest absolute value found in the vector. Note that
 * smallest negative value need to be saturated to preset as int32_t.
 */
static int32_t find_max_abs_int32(int32_t vec[], int vec_length)
{
	int64_t amax = (vec[0] > 0) ? vec[0] : -vec[0];
	int i, val;

	for (i = 1; i < vec_length; i++) {
		amax = (vec[i] > amax) ? vec[i] : amax;
		amax = (-vec[i] > amax) ? -vec[i] : amax;
	}

	val = SATP_INT32(amax); /* Amax is always a positive value */
	return val;
}

/* Count the left shift amount to normalize a 32 bit signed integer value
 * without causing overflow. Input value 0 will result to 31.
 */
static int norm_int32(int32_t val)
{
	int s;
	int32_t n;

	if (!val)
		return 31;

	if (val > 0) {
		n = val << 1;
		s = 0;
		while (n > 0) {
			n = n << 1;
			s++;
		}
	} else {
		n = val << 1;
		s = 0;
		while (n < 0) {
			n = n << 1;
			s++;
		}
	}
	return s;
}

/* Calculate scale and shift to use for FIR coefficients. Scale is applied
 * before write to HW coef RAM. Shift will be programmed to HW register.
 */
static int fir_coef_scale(int32_t *fir_scale, int *fir_shift, int add_shift,
			  const int32_t coef[], int coef_length, int32_t gain)
{
	int32_t aDMIC_MAX;
	int32_t new_aDMIC_MAX;
	int32_t fir_gain;
	int shift;

	/* Multiply gain passed from CIC with output full scale. */
	fir_gain = Q_MULTSR_32X32((int64_t)gain, DMIC_HW_SENS_Q28,
				  DMIC_FIR_SCALE_Q, 28, DMIC_FIR_SCALE_Q);

	/* Find the largest FIR coefficient value. */
	aDMIC_MAX = find_max_abs_int32((int32_t *)coef, coef_length);

	/* Scale DMIC_MAX. tap value with FIR gain. */
	new_aDMIC_MAX = Q_MULTSR_32X32((int64_t)aDMIC_MAX, fir_gain, 31,
				  DMIC_FIR_SCALE_Q, DMIC_FIR_SCALE_Q);
	if (new_aDMIC_MAX <= 0)
		return -EINVAL;

	/* Get left shifts count to normalize the fractional value as 32 bit.
	 * We need right shifts count for scaling so need to invert. The
	 * difference of Q31 vs. used Q format is added to get the correct
	 * normalization right shift value.
	 */
	shift = 31 - DMIC_FIR_SCALE_Q - norm_int32(new_aDMIC_MAX);

	/* Add to shift for coef raw Q31 format shift and store to
	 * configuration. Ensure range (fail should not happen with OK
	 * coefficient set).
	 */
	*fir_shift = -shift + add_shift;
	if (*fir_shift < DMIC_HW_FIR_SHIFT_MIN ||
	    *fir_shift > DMIC_HW_FIR_SHIFT_MAX)
		return -EINVAL;

	/* Compensate shift into FIR coef scaler and store as Q4.20. */
	if (shift < 0)
		*fir_scale = fir_gain << -shift;
	else
		*fir_scale = fir_gain >> shift;

	return 0;
}

/* This function selects with a simple criteria one mode to set up the
 * decimator. For the settings chosen for FIFOs A and B output a lookup
 * is done for FIR coefficients from the included coefficients tables.
 * For some decimation factors there may be several length coefficient sets.
 * It is due to possible restruction of decimation engine cycles per given
 * sample rate. If the coefficients length is exceeded the lookup continues.
 * Therefore the list of coefficient set must present the filters for a
 * decimation factor in decreasing length order.
 *
 * Note: If there is no filter available an error is returned. The parameters
 * should be reviewed for such case. If still a filter is missing it should be
 * added into the included set. FIR decimation with a high factor usually
 * needs compromizes into specifications and is not desirable.
 */
static int select_mode(struct snd_sof_dev *sdev,
		       struct dmic_configuration *cfg,
		       struct matched_modes *modes)
{
	int32_t g_cic;
	int32_t fir_in_max;
	int32_t cic_out_max;
	int32_t gain_to_fir;
	int16_t idx[DMIC_MAX_MODES];
	int16_t *mfir;
	int count;
	int mcic;
	int bits_cic;
	int ret;
	int n;

	/* If there are more than one possibilities select a mode with a preferred
	 * FIR decimation factor. If there are several select mode with highest
	 * ioclk divider to minimize microphone power consumption. The highest
	 * clock divisors are in the end of list so select the last of list.
	 * The minimum OSR criteria used in previous ensures that quality in
	 * the candidates should be sufficient.
	 */
	if (modes->num_of_modes == 0) {
		dev_err(sdev->dev, "%s: no modes available", __func__);
		return -EINVAL;
	}

	/* Valid modes presence is indicated with non-zero decimation
	 * factor in 1st element. If FIR A is not used get decimation factors
	 * from FIR B instead.
	 */
	if (modes->mfir_a[0] > 0)
		mfir = modes->mfir_a;
	else
		mfir = modes->mfir_b;

	/* Search fir_list[] decimation factors from start towards end. The found
	 * last configuration entry with searched decimation factor will be used.
	 */
	count = 0;
	for (n = 0; fir_list[n]; n++) {
		count = find_equal_int16(idx, mfir, fir_list[n]->decim_factor,
					 modes->num_of_modes, 0);
		if (count > 0)
			break;
	}

	if (!count) {
		dev_err(sdev->dev, "%s: No filter for decimate found", __func__);
		return -EINVAL;
	}
	n = idx[count - 1]; /* Option with highest clock divisor and lowest mic clock rate */

	/* Get microphone clock and decimation parameters for used mode from
	 * the list.
	 */
	cfg->clkdiv = modes->clkdiv[n];
	cfg->mfir_a = modes->mfir_a[n];
	cfg->mfir_b = modes->mfir_b[n];
	cfg->mcic = modes->mcic[n];
	cfg->fir_a = NULL;
	cfg->fir_b = NULL;

	/* Find raw FIR coefficients to match the decimation factors of FIR
	 * A and B.
	 */
	if (cfg->mfir_a > 0) {
		cfg->fir_a = get_fir(sdev, cfg, cfg->mfir_a);
		if (!cfg->fir_a) {
			dev_err(sdev->dev, "%s: cannot find FIR coefficients, mfir_a = %u",
				__func__, cfg->mfir_a);
			return -EINVAL;
		}
	}

	if (cfg->mfir_b > 0) {
		cfg->fir_b = get_fir(sdev, cfg, cfg->mfir_b);
		if (!cfg->fir_b) {
			dev_err(sdev->dev, "%s: cannot find FIR coefficients, mfir_b = %u",
				__func__, cfg->mfir_b);
			return -EINVAL;
		}
	}

	/* Calculate CIC shift from the decimation factor specific gain. The
	 * gain of HW decimator equals decimation factor to power of 5.
	 */
	mcic = cfg->mcic;
	g_cic = mcic * mcic * mcic * mcic * mcic;
	if (g_cic < 0) {
		/* Erroneous decimation factor and CIC gain */
		dev_err(sdev->dev, "%s: erroneous decimation factor and CIC gain", __func__);
		return -EINVAL;
	}

	bits_cic = 32 - norm_int32(g_cic);
	cfg->cic_shift = bits_cic - DMIC_HW_BITS_FIR_INPUT;

	/* Calculate remaining gain to FIR in Q format used for gain
	 * values.
	 */
	fir_in_max = DMIC_INT_MAX(DMIC_HW_BITS_FIR_INPUT);
	if (cfg->cic_shift >= 0)
		cic_out_max = g_cic >> cfg->cic_shift;
	else
		cic_out_max = g_cic << -cfg->cic_shift;

	gain_to_fir = (int32_t)((((int64_t)fir_in_max) << DMIC_FIR_SCALE_Q) /
		cic_out_max);

	/* Calculate FIR scale and shift */
	if (cfg->mfir_a > 0) {
		cfg->fir_a_length = cfg->fir_a->length;
		ret = fir_coef_scale(&cfg->fir_a_scale, &cfg->fir_a_shift,
				     cfg->fir_a->shift, cfg->fir_a->coef,
				     cfg->fir_a->length, gain_to_fir);
		if (ret < 0) {
			/* Invalid coefficient set found, should not happen. */
			dev_err(sdev->dev, "%s: invalid coefficient set found", __func__);
			return -EINVAL;
		}
	} else {
		cfg->fir_a_scale = 0;
		cfg->fir_a_shift = 0;
		cfg->fir_a_length = 0;
	}

	if (cfg->mfir_b > 0) {
		cfg->fir_b_length = cfg->fir_b->length;
		ret = fir_coef_scale(&cfg->fir_b_scale, &cfg->fir_b_shift,
				     cfg->fir_b->shift, cfg->fir_b->coef,
				     cfg->fir_b->length, gain_to_fir);
		if (ret < 0) {
			/* Invalid coefficient set found, should not happen. */
			dev_err(sdev->dev, "%s: invalid coefficient set found", __func__);
			return -EINVAL;
		}
	} else {
		cfg->fir_b_scale = 0;
		cfg->fir_b_shift = 0;
		cfg->fir_b_length = 0;
	}

	return 0;
}

/* The FIFO input packer mode (IPM) settings are somewhat different in
 * HW versions. This helper function returns a suitable IPM bit field
 * value to use.
 */
static inline void ipm_helper(const struct sof_intel_dsp_desc *chip_info, int *ipm, int di)
{
	int pdm[DMIC_MAX_HW_CONTROLLERS];
	int i;

	/* Loop number of PDM controllers in the configuration. If mic A
	 * or B is enabled then a pdm controller is marked as active for
	 * this DAI.
	 */
	for (i = 0; i < chip_info->dmic_controller_num; i++) {
		if (dmic_prm[di]->pdm[i].enable_mic_a ||
		    dmic_prm[di]->pdm[i].enable_mic_b)
			pdm[i] = 1;
		else
			pdm[i] = 0;
	}

	/* Set IPM to match active pdm controllers. */
	*ipm = 0;

	if (chip_info->dmic_hw_version == SOF_DMIC_TGL) {
		if (pdm[0] > 0 && pdm[1] == 0)
			*ipm = 0;
		if (pdm[0] > 0 && pdm[1] > 0)
			*ipm = 2;
	} else if (chip_info->dmic_hw_version == SOF_DMIC_MTL) {
		for (i = 0, *ipm = 0; i < chip_info->dmic_controller_num; i++)
			if (pdm[i])
				(*ipm)++;
	}
}

/* Loop number of PDM controllers in the configuration. The function
 * checks if the controller should operate as stereo or mono left (A)
 * or mono right (B) mode. Mono right mode is setup as channel
 * swapped mono left.
 */
static int stereo_helper(int stereo[], int swap[], int controller_num)
{
	int cnt;
	int i;
	int swap_check;
	int ret = 0;

	for (i = 0; i < controller_num; i++) {
		cnt = 0;
		if (dmic_prm[0]->pdm[i].enable_mic_a ||
		    dmic_prm[1]->pdm[i].enable_mic_a)
			cnt++;

		if (dmic_prm[0]->pdm[i].enable_mic_b ||
		    dmic_prm[1]->pdm[i].enable_mic_b)
			cnt++;

		/* Set stereo mode if both mic A anc B are enabled. */
		cnt >>= 1;
		stereo[i] = cnt;

		/* Swap channels if only mic B is used for mono processing. */
		swap[i] = (dmic_prm[0]->pdm[i].enable_mic_b ||
			dmic_prm[1]->pdm[i].enable_mic_b) && !cnt;

		/* Check that swap does not conflict with other DAI request */
		swap_check = dmic_prm[1]->pdm[i].enable_mic_a ||
			dmic_prm[0]->pdm[i].enable_mic_a;

		if (swap_check && swap[i])
			ret = -EINVAL;
	}
	return ret;
}

static int generate_outcontrol(struct snd_sof_dev *sdev, int di,
					const struct sof_intel_dsp_desc *chip_info)
{
	int bfth = 3; /* Should be 3 for 8 entries, 1 is 2 entries */
	int th = 3;
	int ipm;
	int of;
	int val;

	of = (dmic_prm[di]->fifo_bits == 32) ? 2 : 0;

	if (di == 0)
		ipm_helper(chip_info, &ipm, 0);
	else
		ipm_helper(chip_info, &ipm, 1);

	val = OUTCONTROL_TIE(0) |
		OUTCONTROL_SIP(0) |
		OUTCONTROL_FINIT(0) |
		OUTCONTROL_FCI(0) |
		OUTCONTROL_BFTH(bfth) |
		OUTCONTROL_OF(of) |
		OUTCONTROL_TH(th);

	switch (chip_info->dmic_hw_version) {
	case SOF_DMIC_TGL:
		val |= OUTCONTROL_IPM(ipm);
		break;
	case SOF_DMIC_MTL:
		val |= OUTCONTROL_ACE_IPM(ipm);

		if (ipm > 0)
			val |= OUTCONTROL_IPM_SRC_1(0);
		if (ipm > 1)
			val |= OUTCONTROL_IPM_SRC_2(1);
		if (ipm > 2)
			val |= OUTCONTROL_IPM_SRC_3(2);
		if (ipm > 3)
			val |= OUTCONTROL_IPM_SRC_4(3);

		break;
	default:
		dev_err(sdev->dev, "error: unsupported platform %d", chip_info->dmic_hw_version);
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "configure_registers(), OUTCONTROL = %08x", val);

	return val;
}

static int configure_registers(struct snd_sof_dev *sdev, int di, struct dmic_configuration *cfg,
			       struct sof_ipc_dai_config *config,
			       struct sof_dmic_config_blob *blob)
{
	const struct sof_dev_desc *desc = sdev->pdata->desc;
	const struct sof_intel_dsp_desc *chip_info;
	int stereo[DMIC_MAX_HW_CONTROLLERS];
	int swap[DMIC_MAX_HW_CONTROLLERS];
	struct sof_pdm_ctrl_cfg *pdm_ctrl_cfg;
	uint32_t val;
	int32_t ci;
	uint32_t cu;
	int fir_decim;
	int fir_length;
	int length;
	int edge;
	int soft_reset;
	int cic_mute;
	int fir_mute;
	int i;
	int j;
	int ret;
	int dccomp = 1;
	int array_a = 0;
	int array_b = 0;

	chip_info = desc->chip_info;

	/* Normal start sequence */
	soft_reset = 0;
	cic_mute = 0;
	fir_mute = 0;

	dev_dbg(sdev->dev, "dmic configuring registers");

	ret = generate_outcontrol(sdev, di, chip_info);
	if (ret < 0)
		return ret;

	blob->channel_cfg = ret;
	blob->channel_ctrl_mask = BIT(di);
	blob->pdm_ctrl_mask = BIT(config->dmic.num_pdm_active) - 1;

	ret = stereo_helper(stereo, swap, chip_info->dmic_controller_num);
	if (ret < 0) {
		dev_err(sdev->dev, "%s: enable conflict", __func__);
		return ret;
	}

	pdm_ctrl_cfg = blob->pdm_ctrl_cfg;
	for (i = 0; i < chip_info->dmic_controller_num && i < config->dmic.num_pdm_active; i++) {
		/* CIC */
		val = CIC_CONTROL_SOFT_RESET(soft_reset) |
			CIC_CONTROL_CIC_START_B(0) |
			CIC_CONTROL_CIC_START_A(0) |
			CIC_CONTROL_MIC_B_POLARITY(dmic_prm[di]->pdm[i].polarity_mic_a) |
			CIC_CONTROL_MIC_A_POLARITY(dmic_prm[di]->pdm[i].polarity_mic_b) |
			CIC_CONTROL_MIC_MUTE(cic_mute) |
			CIC_CONTROL_STEREO_MODE(stereo[i]);
		pdm_ctrl_cfg->cic_control = val;
		dev_dbg(sdev->dev, "%s, CIC_CONTROL = %08x", __func__, val);

		val = CIC_CONFIG_CIC_SHIFT(cfg->cic_shift + 8) |
			CIC_CONFIG_COMB_COUNT(cfg->mcic - 1);
		pdm_ctrl_cfg->cic_config = val;
		dev_dbg(sdev->dev, "%s, CIC_CONFIG = %08x", __func__, val);

		/* Mono right channel mic usage requires swap of PDM channels
		 * since the mono decimation is done with only left channel
		 * processing active.
		 */
		edge = dmic_prm[di]->pdm[i].clk_edge;
		if (swap[i])
			edge = !edge;

		val = MIC_CONTROL_PDM_CLKDIV(cfg->clkdiv - 2) |
			MIC_CONTROL_PDM_SKEW(dmic_prm[di]->pdm[i].skew) |
			MIC_CONTROL_CLK_EDGE(edge) |
			MIC_CONTROL_PDM_EN_B(dmic_prm[di]->pdm[i].enable_mic_b) |
			MIC_CONTROL_PDM_EN_A(dmic_prm[di]->pdm[i].enable_mic_b);
		pdm_ctrl_cfg->mic_control = val;
		dev_dbg(sdev->dev, "%s, MIC_CONTROL = %08x", __func__, val);

		if (di == 0) {
			/* FIR A */
			fir_decim = DMIC_MAX(cfg->mfir_a - 1, 0);
			fir_length = DMIC_MAX(cfg->fir_a_length - 1, 0);
			val = FIR_CONTROL_A_START(0) |
				FIR_CONTROL_A_ARRAY_START_EN(array_a) |
				FIR_CONTROL_A_DCCOMP(dccomp) |
				FIR_CONTROL_A_MUTE(fir_mute) |
				FIR_CONTROL_A_STEREO(stereo[i]);
			pdm_ctrl_cfg->fir_config[di].fir_control = val;
			dev_dbg(sdev->dev, "%s, FIR_CONTROL_A = %08x", __func__, val);

			val = FIR_CONFIG_A_FIR_DECIMATION(fir_decim) |
				FIR_CONFIG_A_FIR_SHIFT(cfg->fir_a_shift) |
				FIR_CONFIG_A_FIR_LENGTH(fir_length);
			pdm_ctrl_cfg->fir_config[di].fir_config = val;
			dev_dbg(sdev->dev, "%s, FIR_CONFIG_A = %08x", __func__, val);

			val = DC_OFFSET_LEFT_A_DC_OFFS(DCCOMP_TC0);
			pdm_ctrl_cfg->fir_config[di].dc_offset_left = val;
			dev_dbg(sdev->dev, "%s, DC_OFFSET_LEFT_A = %08x", __func__, val);

			val = DC_OFFSET_RIGHT_A_DC_OFFS(DCCOMP_TC0);
			pdm_ctrl_cfg->fir_config[di].dc_offset_right = val;
			dev_dbg(sdev->dev, "%s, DC_OFFSET_RIGHT_A = %08x", __func__, val);

			val = OUT_GAIN_LEFT_A_GAIN(0);
			pdm_ctrl_cfg->fir_config[di].out_gain_left = val;
			dev_dbg(sdev->dev, "%s, OUT_GAIN_LEFT_A = %08x", __func__, val);

			val = OUT_GAIN_RIGHT_A_GAIN(0);
			pdm_ctrl_cfg->fir_config[di].out_gain_right = val;
			dev_dbg(sdev->dev, "%s, OUT_GAIN_RIGHT_A = %08x", __func__, val);

			if (!i) {
				/* Write coef RAM A with scaled coefficient in reverse order */
				length = cfg->fir_a_length;
				for (j = 0; j < length; j++) {
					ci = (int32_t)Q_MULTSR_32X32((int64_t)cfg->fir_a->coef[j],
								     cfg->fir_a_scale, 31,
								     DMIC_FIR_SCALE_Q,
								     DMIC_HW_FIR_COEF_Q);
					cu = FIR_COEF_A(ci);
					pdm_ctrl_cfg->fir_coeffs[length - j - 1] = cu;
				}
			} else
				pdm_ctrl_cfg->reuse_fir_from_pdm = 1;
		}

		if (di == 1) {
			/* FIR B */
			fir_decim = DMIC_MAX(cfg->mfir_b - 1, 0);
			fir_length = DMIC_MAX(cfg->fir_b_length - 1, 0);
			val = FIR_CONTROL_B_START(0) |
				FIR_CONTROL_B_ARRAY_START_EN(array_b) |
				FIR_CONTROL_B_DCCOMP(dccomp) |
				FIR_CONTROL_B_MUTE(fir_mute) |
				FIR_CONTROL_B_STEREO(stereo[i]);
			pdm_ctrl_cfg->fir_config[di].fir_control = val;
			dev_dbg(sdev->dev, "%s, FIR_CONTROL_B = %08x", __func__, val);

			val = FIR_CONFIG_B_FIR_DECIMATION(fir_decim) |
				FIR_CONFIG_B_FIR_SHIFT(cfg->fir_b_shift) |
				FIR_CONFIG_B_FIR_LENGTH(fir_length);
			pdm_ctrl_cfg->fir_config[di].fir_config = val;
			dev_dbg(sdev->dev, "%s, FIR_CONFIG_B = %08x", __func__, val);

			val = DC_OFFSET_LEFT_B_DC_OFFS(DCCOMP_TC0);
			pdm_ctrl_cfg->fir_config[di].dc_offset_left = val;
			dev_dbg(sdev->dev, "%s, DC_OFFSET_LEFT_B = %08x", __func__, val);

			val = DC_OFFSET_RIGHT_B_DC_OFFS(DCCOMP_TC0);
			pdm_ctrl_cfg->fir_config[di].dc_offset_right = val;
			dev_dbg(sdev->dev, "%s, DC_OFFSET_RIGHT_B = %08x", __func__, val);

			val = OUT_GAIN_LEFT_B_GAIN(0);
			pdm_ctrl_cfg->fir_config[di].out_gain_left = val;
			dev_dbg(sdev->dev, "%s, OUT_GAIN_LEFT_B = %08x", __func__, val);

			val = OUT_GAIN_RIGHT_B_GAIN(0);
			pdm_ctrl_cfg->fir_config[di].out_gain_right = val;
			dev_dbg(sdev->dev, "%s, OUT_GAIN_RIGHT_B = %08x", __func__, val);

			if (!i) {
				/* Write coef RAM B with scaled coefficient in reverse order */
				length = cfg->fir_b_length;
				for (j = 0; j < length; j++) {
					ci = (int32_t)Q_MULTSR_32X32((int64_t)cfg->fir_b->coef[j],
								     cfg->fir_b_scale, 31,
								     DMIC_FIR_SCALE_Q,
								     DMIC_HW_FIR_COEF_Q);
					cu = FIR_COEF_B(ci);
					pdm_ctrl_cfg->fir_coeffs[length - j - 1] = cu;
				}
			} else
				pdm_ctrl_cfg->reuse_fir_from_pdm = 1;
		}

		if (!i) {
			pdm_ctrl_cfg = (struct sof_pdm_ctrl_cfg *)((int *)pdm_ctrl_cfg->fir_coeffs +
								    length);
		} else {
			pdm_ctrl_cfg = (struct sof_pdm_ctrl_cfg *)pdm_ctrl_cfg->fir_coeffs;
		}
	}

	return 0;
}

static int sof_ipc4_process_dmic_config(struct snd_sof_dev *sdev,
					struct sof_ipc_dai_config *config,
					struct dmic_configuration *cfg, int di)
{
	const struct sof_dev_desc *desc = sdev->pdata->desc;
	const struct sof_intel_dsp_desc *chip_info;
	struct matched_modes modes_ab;
	struct decim_modes modes_a;
	struct decim_modes modes_b;
	size_t size;
	int i, j, ret = 0;

	if (!dmic_prm[0]) {
		size = sizeof(struct sof_ipc_dai_dmic_params);
		dmic_prm[0] = devm_kzalloc(sdev->dev, DMIC_HW_FIFOS * size, GFP_KERNEL);
		if (!dmic_prm[0]) {
			dev_err(sdev->dev, "dmic_set_config(): prm not initialized");
			return -ENOMEM;
		}
		for (i = 1; i < DMIC_HW_FIFOS; i++)
			dmic_prm[i] = (struct sof_ipc_dai_dmic_params *)
				((uint8_t *)dmic_prm[i - 1] + size);
	}

	/* Copy the new DMIC params header (all but not pdm[]) to persistent.
	 * The last arrived request determines the parameters.
	 */
	memcpy(dmic_prm[di], &config->dmic, sizeof(config->dmic));

	chip_info = desc->chip_info;
	/* copy the pdm controller params from ipc */
	for (i = 0; i < chip_info->dmic_controller_num; i++) {
		dmic_prm[di]->pdm[i].id = i;
		for (j = 0; j < config->dmic.num_pdm_active; j++) {
			/* copy the pdm controller params id the id's match */
			if (dmic_prm[di]->pdm[i].id == config->dmic.pdm[j].id) {
				memcpy(&dmic_prm[di]->pdm[i], &config->dmic.pdm[j],
						sizeof(dmic_prm[di]->pdm[i]));
			}
		}
	}

	dev_dbg(sdev->dev, "%s: prm config->dmic.num_pdm_active = %u", __func__,
		config->dmic.num_pdm_active);
	dev_dbg(sdev->dev, "%s: prm pdmclk_min = %u, pdmclk_max = %u", __func__,
		dmic_prm[di]->pdmclk_min, dmic_prm[di]->pdmclk_max);
	dev_dbg(sdev->dev, "%s: prm duty_min = %u, duty_max = %u", __func__,
		dmic_prm[di]->duty_min, dmic_prm[di]->duty_max);
	dev_dbg(sdev->dev, "%s: prm fifo_fs = %u, fifo_bits = %u", __func__,
		dmic_prm[di]->fifo_fs, dmic_prm[di]->fifo_bits);

	switch (dmic_prm[di]->fifo_bits) {
	case 0:
	case 16:
	case 32:
		break;
	default:
		dev_err(sdev->dev, "dmic_set_config(): fifo_bits EINVAL");
		return -EINVAL;
	}

	/* Match and select optimal decimators configuration for FIFOs A and B
	 * paths. This setup phase is still abstract. Successful completion
	 * points struct cfg to FIR coefficients and contains the scale value
	 * to use for FIR coefficient RAM write as well as the CIC and FIR
	 * shift values.
	 */
	find_modes(sdev, &modes_a, dmic_prm[0]->fifo_fs, di);
	if (modes_a.num_of_modes == 0 && dmic_prm[0]->fifo_fs > 0) {
		dev_err(sdev->dev, "%s: No modes found for FIFO A", __func__);
		return -EINVAL;
	}

	find_modes(sdev, &modes_b, dmic_prm[1]->fifo_fs, di);
	if (modes_b.num_of_modes == 0 && dmic_prm[1]->fifo_fs > 0) {
		dev_err(sdev->dev, "%s: No modes found for FIFO B", __func__);
		return -EINVAL;
	}

	match_modes(&modes_ab, &modes_a, &modes_b);
	ret = select_mode(sdev, cfg, &modes_ab);
	if (ret < 0) {
		dev_err(sdev->dev, "dmic_set_config(): select_mode() failed");
		return -EINVAL;
	}

	dev_dbg(sdev->dev, "dmic_set_config(), cfg clkdiv = %u, mcic = %u",
		 cfg->clkdiv, cfg->mcic);
	dev_dbg(sdev->dev, "dmic_set_config(), cfg mfir_a = %u, mfir_b = %u",
		 cfg->mfir_a, cfg->mfir_b);
	dev_dbg(sdev->dev, "dmic_set_config(), cfg cic_shift = %u",
		 cfg->cic_shift);
	dev_dbg(sdev->dev, "dmic_set_config(), cfg fir_a_shift = %u, cfg->fir_b_shift = %u",
		 cfg->fir_a_shift, cfg->fir_b_shift);
	dev_dbg(sdev->dev, "dmic_set_config(), cfg fir_a_length = %u, fir_b_length = %u",
		 cfg->fir_a_length, cfg->fir_b_length);

	return 0;
}

int sof_ipc4_generate_dmic_config(struct snd_sof_dev *sdev, struct sof_ipc4_dai *ipc4_dai,
		struct snd_pcm_hw_params *params,
		int lp_mode)
{
	const struct sof_dev_desc *desc = sdev->pdata->desc;
	const struct sof_intel_dsp_desc *chip_info;
	struct sof_dmic_config_data *config_data;
	struct sof_ipc4_module_copier *copier;
	struct dmic_configuration dmic_config;
	struct sof_ipc_dai_config *dai_config;
	int channels, width, rate;
	int size, di, ret;

	copier = &ipc4_dai->copier;
	dai_config = ipc4_dai->dai.dai_config;

	channels = params_channels(params);
	width = params_width(params);
	rate = params_rate(params);

	dai_config->dmic.fifo_fs = rate;
	dai_config->dmic.fifo_bits = width;

	if ((BIT(channels) - 1) & BIT(channels)) {
		dev_err(sdev->dev, "error: unsupported channel count %d", channels);
		return -EINVAL;
	}

	if (channels >= 1)
		dai_config->dmic.pdm[0].enable_mic_a = 1;

	if (channels >= 2) {
		dai_config->dmic.pdm[0].enable_mic_a = 1;
		dai_config->dmic.pdm[0].enable_mic_b = 1;
	}
	if (channels >= 4) {
		dai_config->dmic.pdm[1].enable_mic_a = 1;
		dai_config->dmic.pdm[1].enable_mic_b = 1;
	}
	if (channels >= 8) {
		dai_config->dmic.pdm[2].enable_mic_a = 1;
		dai_config->dmic.pdm[2].enable_mic_b = 1;
		dai_config->dmic.pdm[3].enable_mic_a = 1;
		dai_config->dmic.pdm[3].enable_mic_b = 1;
	}

	dai_config->dmic.num_pdm_active = (channels >> 1);

	switch (rate) {
	case 16000:
		di = 1;
		break;
	case 48000:
		di = 0;
		break;
	default:
		dev_err(sdev->dev, "error: unsupported rate %d", rate);
		return -EINVAL;
	}

	size = sof_ipc4_process_dmic_config(sdev, dai_config, &dmic_config, di);
	if (size < 0) {
		dev_err(sdev->dev, "error: failed to process dmic config size %d, rate %d",
			size, rate);
		return -EINVAL;
	}

	chip_info = desc->chip_info;
	if (chip_info->dmic_hw_version == SOF_DMIC_TGL)
		dai_config->dmic.num_pdm_active = chip_info->dmic_controller_num;

	/* blob is a variable length data */
	size = sizeof(struct sof_dmic_config_data);
	size += sizeof(struct sof_pdm_ctrl_cfg) * dai_config->dmic.num_pdm_active;
	size += (dmic_config.fir_a_length + dmic_config.fir_b_length) * sizeof(uint32_t);
	dev_dbg(sdev->dev, "dmic config data size = %d", size);
	config_data = devm_kzalloc(sdev->dev, size, GFP_KERNEL);

	/* Struct reg contains a mirror of actual HW registers. Determine
	 * register bits configuration from decimator configuration and the
	 * requested parameters.
	 */
	ret = configure_registers(sdev, di, &dmic_config, dai_config,
				  &config_data->dmic_config_blob);
	if (ret < 0) {
		dev_err(sdev->dev, "dmic_set_config(): cannot configure registers");
		return ret;
	}

	config_data->gtw_attributes.lp_buffer_alloc = lp_mode;
	config_data->dmic_config_blob.ts_group[0] = 0xFFFF3210;
	config_data->dmic_config_blob.ts_group[1] = 0xFFFFFF10;
	config_data->dmic_config_blob.ts_group[2] = 0xFFFFFF32;
	config_data->dmic_config_blob.ts_group[3] = 0xFFFFFFFF;
	config_data->dmic_config_blob.clock_on_delay = 3;

	ipc4_dai->copier_config = (uint32_t *)config_data;
	copier->gtw_cfg.config_length = size >> 2;

	return 0;
}

