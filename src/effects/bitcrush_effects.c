#include <stdio.h>
#include "bitcrush_effects.h"

const ControlDef bitcrusher_controls[BITCRSH_NUM_CONTROLS] = {
	[BITCRSH_CTRL_GAIN] = { .name = "btcrsh_gain", .label = "Gain", .type = CTRL_KNOB, .row = DISTORTION_ROW_NUM, .default_value = DFLT_BCRSH_GAIN_SET, .update_func = set_btcrsh_gain, .units = "dB" },
	[BITCRSH_CTRL_BTDPTH] = { .name = "btcrsh_btdepth", .label = "Bit Crush", .type = CTRL_KNOB, .row = DISTORTION_ROW_NUM, .default_value = DFLT_BCRSH_BTDPTH_SET, .update_func = set_btcrsh_depth, .units = "bit" },
	[BITCRSH_CTRL_RATE] = { .name = "btcrsh_smp_rate", .label = "Sample Crush", .type = CTRL_KNOB, .row = DISTORTION_ROW_NUM, .default_value = DFLT_SAMPLE_RATE_SET, .update_func = set_btcrsh_sample_rate, .units = "Hz" },
	[BITCRSH_CTRL_BP] = { .name = "bit_bandpass", .label = "Filter", .type = CTRL_KNOB, .row = DISTORTION_ROW_NUM, .default_value = DFLT_BCRSH_BP_SET, .update_func = set_btcrsh_bp, .units = "Hz" },
	[BITCRSH_CTRL_LVL] = { .name = "btcrsh_level", .label = "Level", .type = CTRL_KNOB, .row = DISTORTION_ROW_NUM, .default_value = DFLT_BCRSH_LVL_SET, .update_func = set_btcrsh_lvl, .units = "dB" },
	[BITCRSH_CTRL_BYPASS] = { .name = "btcrsh_bypass", .label = "Bypass", .type = CTRL_SWITCH, .row = DISTORTION_ROW_NUM, .default_value = OFF, .update_func = set_btcrsh_bypass, .units = "" }
};

void bitcrush_init(bit_engine* b_engine, int sample_rate) {
	
	effects_engine base;
	effects_engine_init(&base, "bitcrusher");
	base.controls = bitcrusher_controls;
	base.num_controls = BITCRSH_NUM_CONTROLS;
	base.colour = "#8B4513";
	b_engine->base = base;

	set_btcrsh_depth(&b_engine->base, DFLT_BCRSH_BTDPTH_SET);

	b_engine->held_sample = 0.0f;
	b_engine->phase = 1;										// starts at 1 so we grabe the very first sample when processing starts
	b_engine->sample_rate = sample_rate;
	set_btcrsh_sample_rate(&b_engine->base, DFLT_SAMPLE_RATE_SET);

	b_engine->lpf = bq_new(LOWPASS, MAX_BIT_LP, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	b_engine->hpf1 = bq_new(HIGHPASS, MIN_BIT_HP, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	b_engine->hpf2 = bq_new(HIGHPASS, MIN_BIT_HP, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	b_engine->bp_make_gain = _0dB;

	set_btcrsh_gain(&b_engine->base, DFLT_BCRSH_GAIN_SET);

	b_engine->bypass = OFF;
}

void process_bitcrush(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

	bit_engine* b_engine = (bit_engine*)engine;

	float sample;
	for(int i = 0; i < buffer_size; i++) {

		// Sample rate crushing
		b_engine->phase = b_engine->phase + b_engine->rate_ratio;
		if (b_engine->phase >= 1.0) {
			b_engine->held_sample = in_buf[i];
			b_engine->phase = b_engine->phase - 1;
		}
		sample = b_engine->held_sample;

		// apply gain for bit crushing
		sample = sample * b_engine->gain;
		sample = fmaxf(-1.0f, fminf(1.0f, sample));			// clamp to {-1, 1}
		// quantize the signal
		sample = sample * b_engine->scale;
		sample = roundf(sample);
		sample = sample / b_engine->scale;
		// normalize level
		sample = sample * b_engine->inv_gain;

		// Bandpass filter
		sample = bq_process(b_engine->hpf1, sample);
		sample = bq_process(b_engine->hpf2, sample);
		sample = bq_process(b_engine->lpf, sample);

		// apply make up gain
		sample = sample * b_engine->bp_make_gain;

		out_buf[i] = sample * b_engine->level;
	}
}

void bitcrush_free(bit_engine* b_engine) {

	bq_destroy(b_engine->hpf1);
	bq_destroy(b_engine->hpf2);
	bq_destroy(b_engine->lpf);
}

float set_btcrsh_depth(effects_engine* engine, float bit_depth) {

	bit_engine* b_engine = (bit_engine*)engine;

	// float new_bit_depth = MAX_BIT_DEPTH - (bit_depth * (MAX_BIT_DEPTH - 1));
	float new_bit_depth = 1 - bit_depth;
	new_bit_depth = convert_to_range(new_bit_depth, MIN_BCRSH_BTDPTH, MAX_BCRSH_BTDPTH);
	b_engine->bit_depth = new_bit_depth;
	b_engine->levels = powf(2, new_bit_depth);
	b_engine->scale = (b_engine->levels / 2);

	return new_bit_depth;
}

float set_btcrsh_gain(effects_engine* engine, float gain) {

	bit_engine* b_engine = (bit_engine*)engine;

	float new_gain = linear_to_log(gain, MIN_BCRSH_GAIN, MAX_BCRSH_GAIN);
	b_engine->gain = new_gain;
	b_engine->inv_gain = 1.0f / new_gain;

	return linear_gain_to_decibels(new_gain);
}

float set_btcrsh_sample_rate(effects_engine* engine, float sample_rate) {

	bit_engine* b_engine = (bit_engine*)engine;

	float new_rate = (1 - sample_rate);												//flips the knob value
	new_rate = convert_to_range(new_rate, MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);
	b_engine->rate_ratio = new_rate;

	return new_rate * b_engine->sample_rate;
}

float set_btcrsh_bp(effects_engine* engine, float band) {

	bit_engine* b_engine = (bit_engine*)engine;

	float band_lpfreq, band_hpfreq, gain;

	// update make up gain
	gain = linear_to_log(band, _0dB, MAX_BP_MK_GAIN);
	b_engine->bp_make_gain = gain;

	// update filters
	band_hpfreq = linear_to_log(band, MIN_BIT_HP, MAX_BIT_HP);
	band = 1 - band;															// invert the control signal 
	band_lpfreq = linear_to_log(band, MIN_BIT_LP, MAX_BIT_LP);
	
	bq_update(b_engine->hpf1, HIGHPASS, band_hpfreq, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	bq_update(b_engine->hpf2, HIGHPASS, band_hpfreq, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	bq_update(b_engine->lpf, LOWPASS, band_lpfreq, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);

	return band_lpfreq - band_hpfreq;
}

float set_btcrsh_lvl(effects_engine* engine, float level) {

	bit_engine* b_engine = (bit_engine*)engine;

	float new_level = linear_to_log(level, MIN_BCRSH_LVL, MAX_BCRSH_LVL);
	b_engine->level = new_level;

	return linear_gain_to_decibels(new_level);
}

float set_btcrsh_bypass(effects_engine* engine, float bypass) {

	bit_engine* b_engine = (bit_engine*)engine;

	int new_bypass = (int)bypass;
	b_engine->bypass = new_bypass;

	return new_bypass;
}

