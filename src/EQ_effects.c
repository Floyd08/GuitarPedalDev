#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "effects_engine.h"
#include "dsp_tools.h"
#include "EQ_effects.h"

const ControlDef EQ_controls[EQ_NUM_CONTROLS] = {
	[EQ_CTRL_B_GAIN] = { .name = "eq_b_gain", .label = "Bass", .type = CTRL_KNOB, .row = 1, .default_value = 0.5, .update_func = EQ_update_lsf_gain },					
	[EQ_CTRL_M_GAIN] = { .name = "eq_m_gain", .label = "Mid", .type = CTRL_KNOB, .row = 1, .default_value = 0.5, .update_func = EQ_update_mpf_gain },					
	[EQ_CTRL_T_GAIN] = { .name = "eq_t_gain", .label = "Treble", .type = CTRL_KNOB, .row = 1, .default_value = 0.5, .update_func = EQ_update_hsf_gain },					
	[EQ_CTRL_B_FREQ] = { .name = "eq_b_freq", .label = "Bass Sweep", .type = CTRL_KNOB, .row = 2, .default_value = 0.5, .update_func = EQ_update_lsf_centre },						
	[EQ_CTRL_M_FREQ] = { .name = "eq_m_freq", .label = "Mid Sweep", .type = CTRL_KNOB, .row = 2, .default_value = 0.5, .update_func = EQ_update_mpf_centre },						
	[EQ_CTRL_T_FREQ] = { .name = "eq_t_freq", .label = "Treble Sweep", .type = CTRL_KNOB, .row = 2, .default_value = 0.5, .update_func = EQ_update_hsf_centre },						
};

void EQ_init(EQ_engine* eq, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "equalization");
	base.controls = EQ_controls;
	base.num_controls = EQ_NUM_CONTROLS;

	eq->base = base;
	eq->sample_rate = sample_rate;

	eq->lsf = bq_new(LOWSHELF, DFLT_LS_CF, BUTTERWORTH_Q, DFLT_GAIN, sample_rate);
	EQ_update_lsf_gain(&eq->base, DFLT_GAIN);
	EQ_update_lsf_centre(&eq->base, DFLT_LS_CF);

	eq->hsf = bq_new(HIGHSHELF, DFLT_HS_CF, BUTTERWORTH_Q, DFLT_GAIN, sample_rate);
	EQ_update_hsf_gain(&eq->base, DFLT_GAIN);
	EQ_update_hsf_centre(&eq->base, DFLT_HS_CF);

	eq->mpf = bq_new(PEAK, DFLT_MP_CF, MID_Q, DFLT_GAIN, sample_rate);
	EQ_update_mpf_gain(&eq->base, DFLT_GAIN);
	EQ_update_mpf_centre(&eq->base, DFLT_MP_CF);

}

void process_EQ(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size) {

	//printf("processing EQ\n");

	EQ_engine* eq = (EQ_engine*)engine;
	
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
	//printf("EQ processed\n");
}

// These Update functions expect a linear range, between 0 - 1, coming in from the UI
void EQ_update_lsf_gain(effects_engine* engine, float new_gain) {
	EQ_engine* eq = (EQ_engine*)engine;
	new_gain = EQ_convert_dB(new_gain);
	bq_update(eq->lsf, LOWSHELF, eq->lsf_centre, BUTTERWORTH_Q, new_gain, eq->sample_rate);
	eq->lsf_gain = new_gain;
}

void EQ_update_hsf_gain(effects_engine* engine, float new_gain) {
	EQ_engine* eq = (EQ_engine*)engine;
	new_gain = EQ_convert_dB(new_gain);
	bq_update(eq->hsf, HIGHSHELF, eq->hsf_centre, BUTTERWORTH_Q, new_gain, eq->sample_rate);
	eq->hsf_gain = new_gain;
}

void EQ_update_mpf_gain(effects_engine* engine, float new_gain) {
	EQ_engine* eq = (EQ_engine*)engine;
	new_gain = EQ_convert_dB(new_gain);
	bq_update(eq->mpf, PEAK, eq->mpf_centre, MID_Q, new_gain, eq->sample_rate);
	eq->mpf_gain = new_gain;
}

// These Update functions expect a linear range, between 0 - 1, coming in from the UI
void EQ_update_lsf_centre(effects_engine* engine, float new_centre_freq) {
	EQ_engine* eq = (EQ_engine*)engine;
	new_centre_freq = EQ_convert_Hz(new_centre_freq, DFLT_LS_MIN, DFLT_LS_MAX);
	bq_update(eq->lsf, LOWSHELF, new_centre_freq, BUTTERWORTH_Q, eq->lsf_gain, eq->sample_rate);
	eq->lsf_centre = new_centre_freq;
}

void EQ_update_mpf_centre(effects_engine* engine, float new_centre_freq) {
	EQ_engine* eq = (EQ_engine*)engine;
	new_centre_freq = EQ_convert_Hz(new_centre_freq, DFLT_MP_MIN, DFLT_MP_MAX);
	bq_update(eq->mpf, PEAK, new_centre_freq, MID_Q, eq->mpf_gain, eq->sample_rate);
	eq->mpf_centre = new_centre_freq;
}

void EQ_update_hsf_centre(effects_engine* engine, float new_centre_freq) {
	EQ_engine* eq = (EQ_engine*)engine;
	new_centre_freq = EQ_convert_Hz(new_centre_freq, DFLT_HS_MIN, DFLT_HS_MAX);
	bq_update(eq->hsf, HIGHSHELF, new_centre_freq, BUTTERWORTH_Q, eq->hsf_gain, eq->sample_rate);
	eq->hsf_centre = new_centre_freq;
}


float EQ_convert_Hz(float value, float new_min, float new_max) {

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