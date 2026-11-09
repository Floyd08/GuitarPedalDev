#include <stdio.h>
#include "bitcrush_effects.h"

const ControlDef bitcrusher_controls[BITCRSH_NUM_CONTROLS] = {
	[BITCRSH_CTRL_GAIN] = { .name = "bit_gain", .label = "Gain", .type = CTRL_KNOB, .row = 6, .default_value = 0.01, .update_func = set_bit_gain },
	[BITCRSH_CTRL_BTDPTH] = { .name = "bit_crush", .label = "Bit Crush", .type = CTRL_KNOB, .row = 6, .default_value = MAX_BIT_DEPTH_FL, .update_func = set_bit_depth },
	[BITCRSH_CTRL_RATE] = { .name = "sample_crush", .label = "Sample Crush", .type = CTRL_KNOB, .row = 6, .default_value = 0.01, .update_func = set_sample_rate },
	[BITCRSH_CTRL_BP] = { .name = "bit_bandpass", .label = "Filter", .type = CTRL_KNOB, .row = 6, .default_value = 0.01, .update_func = set_bit_bp }
};

void bitcrush_init(bit_engine* b_engine, int sample_rate) {
	
	effects_engine base;
	effects_engine_init(&base, "bitcrusher");
	base.controls = bitcrusher_controls;
	base.num_controls = BITCRSH_NUM_CONTROLS;
	b_engine->base = base;

	b_engine->gain = _0dB;
	b_engine->bit_depth = MAX_BIT_DEPTH;
	b_engine->levels = powf(2, b_engine->bit_depth);
	b_engine->scale = (b_engine->levels / 2);

	b_engine->rate_ratio = DFLT_RATE_RTIO;
	b_engine->held_sample = 0.0f;
	b_engine->phase = 1;										// starts at 1 so we grabe the very first sample when processing starts
	b_engine->sample_rate = sample_rate;

	b_engine->lpf = bq_new(LOWPASS, MAX_BIT_LP, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	b_engine->hpf1 = bq_new(HIGHPASS, MIN_BIT_HP, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	b_engine->hpf2 = bq_new(HIGHPASS, MIN_BIT_HP, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	b_engine->bp_make_gain = _0dB;

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

		// bit crushing
		sample = sample * b_engine->gain;
		// sample = fmaxf(-1.0f, fminf(1.0f, sample));			// clamp to {-1, 1}
		sample = sample * b_engine->scale;
		sample = roundf(sample);
		sample = sample / b_engine->scale;

		// Bandpass filter
		sample = bq_process(b_engine->hpf1, sample);
		sample = bq_process(b_engine->hpf2, sample);
		sample = bq_process(b_engine->lpf, sample);

		// apply make up gain
		sample = sample * b_engine->bp_make_gain;

		out_buf[i] = sample;
	}
}

void set_bit_depth(effects_engine* engine, float bit_depth) {

	bit_engine* b_engine = (bit_engine*)engine;

	float new_bit_depth = MAX_BIT_DEPTH - (bit_depth * (MAX_BIT_DEPTH - 1));
	b_engine->bit_depth = new_bit_depth;
	b_engine->levels = powf(2, new_bit_depth);
	b_engine->scale = (b_engine->levels / 2);
}

void set_bit_gain(effects_engine* engine, float gain) {

	bit_engine* b_engine = (bit_engine*)engine;

	float new_gain = linear_to_log(gain, _0dB, plus_12dB);
	b_engine->gain = new_gain;
}

void set_sample_rate(effects_engine* engine, float sample_rate) {

	bit_engine* b_engine = (bit_engine*)engine;

	float new_rate = (1 - sample_rate);												//flips the knob value
	new_rate = convert_to_range(new_rate, MIN_SAMPLE_RATE, MAX_SAMPLE_RATE);
	b_engine->rate_ratio = new_rate;
}

void set_bit_bp(effects_engine* engine, float band) {

	bit_engine* b_engine = (bit_engine*)engine;

	float band_lpfreq, band_hpfreq, gain = MAX_BP_MK_GAIN;

	// update make up gain
	gain = gain * band;
	b_engine->bp_make_gain = gain;

	// update filters
	band_hpfreq = convert_to_range(band, MIN_BIT_HP, MAX_BIT_HP);
	band = 1 - band;															// invert the control signal 
	band_lpfreq = convert_to_range(band, MIN_BIT_LP, MAX_BIT_LP);
	
	bq_update(b_engine->hpf1, HIGHPASS, band_hpfreq, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	bq_update(b_engine->hpf2, HIGHPASS, band_hpfreq, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
	bq_update(b_engine->lpf, LOWPASS, band_lpfreq, BIT_BP_Q, UNITY_GAIN, b_engine->sample_rate);
}
