#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "EQ_effects.h"

void EQ_init(EQ_engine* eq, int sample_rate) {

	eq->lsf = bq_new(LOWSHELF, DFLT_LS_CF, BUTTERWORTH_Q, DFLT_GAIN, sample_rate);
	eq->lsf_gain = DFLT_GAIN;
	eq->lsf_centre = DFLT_LS_CF;

	eq->hsf = bq_new(HIGHSHELF, DFLT_HS_CF, BUTTERWORTH_Q, DFLT_GAIN, sample_rate);
	eq->hsf_gain = DFLT_GAIN;
	eq->hsf_centre = DFLT_HS_CF;

	eq->mpf = bq_new(PEAK, DFLT_MP_CENTRE, MID_Q, DFLT_GAIN, sample_rate);
	eq->mpf_gain = DFLT_GAIN;
	eq->mpf_centre = DFLT_MP_CENTRE;

	eq->sample_rate = sample_rate;
}

void process_EQ(EQ_engine* eq, float *in_buf, float *out_buf, int buffer_size) {
	
	float sample;
	//float max_abs_val = 0, abs_sample;
	for (int i = 0; i < buffer_size; i++) {
		
		sample = in_buf[i];
		sample = bq_process(eq->lsf, sample);
		sample = bq_process(eq->hsf, sample);
		sample = bq_process(eq->mpf, sample);
		sample = tanhf(sample);								//prevents hard clipping if the EQ drives the signal too hard
		out_buf[i] = sample;								//This is unlikely though, unless a very high gain signal is sent as input
		// abs_sample = fabsf(sample);
		// max_abs_val = fmaxf(max_abs_val, abs_sample);
	}
	// printf("max absolute value of this block: %f\n", max_abs_val);
}

void EQ_update_filter_gain(EQ_engine* eq, float new_gain, FILTER_TYPES f_type) {
	
	new_gain = EQ_convert_dB(new_gain);
	if(f_type == LOWSHELF) {
		bq_update(eq->lsf, LOWSHELF, eq->lsf_centre, BUTTERWORTH_Q, new_gain, eq->sample_rate);
		eq->lsf_gain = new_gain;
	}
	else if(f_type == HIGHSHELF) {
		bq_update(eq->hsf, HIGHSHELF, eq->hsf_centre, BUTTERWORTH_Q, new_gain, eq->sample_rate);
		eq->hsf_gain = new_gain;
	}
	else if(f_type == PEAK) {
		bq_update(eq->mpf, PEAK, eq->mpf_centre, MID_Q, new_gain, eq->sample_rate);
		eq->mpf_gain = new_gain;
	}
	else {
		printf("Filter gain update failed. No such filter\n");
	}
}

void EQ_update_filter_centre(EQ_engine* eq, float new_centre_freq, FILTER_TYPES f_type) {

	if(f_type == LOWSHELF) {
		new_centre_freq = EQ_convert_Hz(new_centre_freq, 20, 400);
		bq_update(eq->lsf, LOWSHELF, new_centre_freq, BUTTERWORTH_Q, eq->lsf_gain, eq->sample_rate);
		eq->lsf_centre = new_centre_freq;
	}
	else if(f_type == HIGHSHELF) {
		new_centre_freq = EQ_convert_Hz(new_centre_freq, 2000, 20000);
		bq_update(eq->hsf, HIGHSHELF, new_centre_freq, BUTTERWORTH_Q, eq->hsf_gain, eq->sample_rate);
		eq->hsf_centre = new_centre_freq;
	}
	else if(f_type == PEAK) {
		new_centre_freq = EQ_convert_Hz(new_centre_freq, 500, 2500);
		bq_update(eq->mpf, PEAK, new_centre_freq, MID_Q, eq->mpf_gain, eq->sample_rate);
		eq->mpf_centre = new_centre_freq;
	}
	else {
		printf("Centre frequency update failed. No such filter\n");
	}
}

float EQ_convert_Hz(float value, float new_min, float new_max) {

	//float new_min = 500, new_max = 2500;
	float new_value = value * (new_max - new_min) + new_min;
	return new_value;
}

float EQ_convert_dB(float value) {

	float dB_range;
	dB_range = value * (MAX_GAIN_EQ - MIN_GAIN_EQ) + MIN_GAIN_EQ;
	return dB_range;
}

void EQ_free(EQ_engine* eq) {

	bq_destroy(eq->lsf);
	bq_destroy(eq->hsf);
	bq_destroy(eq->mpf);
	free(eq);
}