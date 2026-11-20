#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "amp_model.h"

const ControlDef combo_amp_controls[COMBO_AMP_NUM_CONTROLS] = {
	// [AMP_CTRL_GAIN] = { .name = "amp_gain", .label = "Gain", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = 0.17f, .update_func = set_preamp_gain, .units = "dB" },
	[COMBO_AMP_CTRL_BASS] = { .name = "amp_bass", .label = "Bass", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = DFLT_GAIN_LINEAR, .update_func = set_preamp_bass, .units = "dB"  },
	[COMBO_AMP_CTRL_MID] = { .name = "amp_mid", .label = "Mid", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = DFLT_GAIN_LINEAR, .update_func = set_preamp_mid, .units = "dB"  },
	[COMBO_AMP_CTRL_TRBL] = { .name = "amp_treble", .label = "Treble", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = DFLT_GAIN_LINEAR, .update_func = set_preamp_treble, .units = "dB"  },
	[COMBO_AMP_CTRL_MID_SWP] = { .name = "amp_mid_swp", .label = "Mid Sweep", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = 0.2, .update_func = set_preamp_mid_centre, .units = "Hz" },
	[COMBO_AMP_CTRL_DRVE] = { .name = "amp_drive", .label = "Drive", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = 0.17f, .update_func = set_preamp_drive, .units = "dB" },
	[COMBO_AMP_CTRL_LVL] = { .name = "amp_level", .label = "Level", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = DFLT_PRAMP_LVL_SET, .update_func = set_preamp_level, .units = "dB" }
};

const ControlDef amp_head_controls[AMP_HEAD_NUM_CONTROLS] = {
	[AMP_HEAD_CTRL_BASS] = { .name = "amphd_bass", .label = "Bass", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = DFLT_GAIN_LINEAR, .update_func = set_amp_head_bass, .units = "dB"  },
	[AMP_HEAD_CTRL_MID] = { .name = "amphd_mid", .label = "Mid", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = DFLT_GAIN_LINEAR, .update_func = set_amp_head_mid, .units = "dB"  },
	[AMP_HEAD_CTRL_TRBL] = { .name = "amphd_treble", .label = "Treble", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = DFLT_GAIN_LINEAR, .update_func = set_amp_head_treble, .units = "dB"  },
	[AMP_HEAD_CTRL_MID_SWP] = { .name = "amphd_mid_swp", .label = "Mid Sweep", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = 0.2, .update_func = set_amp_head_mid_centre, .units = "Hz" },
	[AMP_HEAD_CTRL_DRVE] = { .name = "amhdp_drive", .label = "Drive", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = 0.17f, .update_func = set_amp_head_drive, .units = "dB" },
	[AMP_HEAD_CTRL_LVL] = { .name = "amphd_level", .label = "Level", .type = CTRL_KNOB, .row = AMP_ROW_NUM, .default_value = DFLT_PRAMP_LVL_SET, .update_func = set_amp_head_level, .units = "dB" }
};

void tone_stack_init(tone_stack* tone, int sample_rate) {
	
	tone->bass_lsf = op_new(OP_LOWSHELF, DFLT_BASS_FREQ, DFLT_GAIN_dB, sample_rate);
	tone->bass_gain = DFLT_GAIN_dB;

	tone->mid_peak = bq_new(PEAK, DFLT_MID_FREQ, BUTTERWORTH_Q, DFLT_GAIN_dB, sample_rate);
	tone->mid_gain = DFLT_GAIN_dB;
	tone->mid_freq = DFLT_MID_FREQ;

	tone->treble_hsf = op_new(OP_HIGHSHELF, DFLT_TREBLE_FREQ, DFLT_GAIN_dB, sample_rate);
	tone->treble_gain = DFLT_GAIN_dB;

	tone->sample_rate = sample_rate;
}

float process_tone_stack(tone_stack* tone, float sample) {

	sample = op_process(tone->bass_lsf, sample);
	sample = op_process(tone->treble_hsf, sample);
	sample = bq_process(tone->mid_peak, sample);
	return sample;	
}

void pre_amp_init(pre_amp* pre_amp, int sample_rate, int buffer_size) {

	int oversample_rate = sample_rate * OVERSAMPLING_FACTOR;

	tone_stack_init(&pre_amp->tone, oversample_rate);
	pre_amp->gain = _0dB;
	pre_amp->drive = _0dB;
	pre_amp->level = _0dB;
	pre_amp->input_hpf = op_new(OP_HIGHPASS, 60, UNITY_GAIN, oversample_rate);
	pre_amp->interstage_hpf = op_new(OP_HIGHPASS, 150, UNITY_GAIN, oversample_rate);
	pre_amp->interstage_lpf = op_new(OP_LOWPASS, 7000, UNITY_GAIN, oversample_rate);
	pre_amp->bias = 0.15f;
	pre_amp->bias_precomp = tanhf(pre_amp->bias);
	
	pre_amp->oversampling_buffer = calloc( buffer_size * OVERSAMPLING_FACTOR, sizeof(float) );
	ai_aa_filter_init(&pre_amp->image_filter, OVERSAMPLING_FACTOR, sample_rate);
	ai_aa_filter_init(&pre_amp->alias_filter, OVERSAMPLING_FACTOR, sample_rate);
	// pre_amp->image_filter = alias_init(OVERSAMPLING_FACTOR, oversample_rate);
	// pre_amp->alias_filter = alias_init(OVERSAMPLING_FACTOR, oversample_rate);
}

void process_pre_amp(pre_amp* pre_amp, float *in_buf, float *out_buf, int buffer_size) {

	float sample;

	upsample(in_buf, buffer_size, pre_amp->oversampling_buffer, OVERSAMPLING_FACTOR);
	int oversampling_buffer_length = buffer_size * OVERSAMPLING_FACTOR;

	for (int i = 0; i < oversampling_buffer_length; i++) {

		sample = pre_amp->oversampling_buffer[i];
		// sample = bq_process(pre_amp->image_filter, sample);
		sample = process_aiaa_filter(&pre_amp->image_filter, sample);
		sample = sample * OVERSAMPLING_FACTOR;

		// input filter
		sample = op_process(pre_amp->input_hpf, sample);
		// gain stage 1
		sample = sample * pre_amp->gain;
		sample = soft_knee_distortion(sample);
		// interstage filters
		sample = op_process(pre_amp->interstage_hpf, sample);
		sample = op_process(pre_amp->interstage_lpf, sample);
		// tone stack
		sample = process_tone_stack(&pre_amp->tone, sample);
		// gain stage 2
		sample = sample * pre_amp->drive;
		sample = sample + pre_amp->bias;
		sample = tanh_distortion(sample) - pre_amp->bias_precomp;
		sample = sample * pre_amp->level;

		//sample = bq_process(pre_amp->alias_filter, sample);
		sample = process_aiaa_filter(&pre_amp->alias_filter, sample);
		pre_amp->oversampling_buffer[i] = sample;
	}

	downsample_mono(pre_amp->oversampling_buffer, OVERSAMPLING_FACTOR, out_buf, buffer_size);
}

void power_amp_init(power_amp* p_amp, int sample_rate) {

	envelope_follower_init(&p_amp->e_fol, POWER_AMP_ATTACK_MS, POWER_AMP_RELEASE_MS, sample_rate);

	p_amp->rail_depth = RAIL_DEPTH;
	p_amp->knee = RAIL_KNEE;
	p_amp->rail_floor = RAIL_FLOOR;
	p_amp->power = DFLT_POWER;
	p_amp->power_coeff = 1.0f - expf(-1.0f / (POWER_CATCHUP * sample_rate));

}

void process_power_amp(power_amp* p_amp, float *in_buf, float *out_buf, int buffer_size) {

	float sample, output;
	for (int i = 0; i < buffer_size; i++) {

		sample = in_buf[i];

		// get the current envelope state(the current load )
		float rail = 1.0f - p_amp->rail_depth * p_amp->e_fol.state;		//the current rail "voltage"
		if (rail < p_amp->rail_floor) {
			rail = p_amp->rail_floor;
		}

		// apply soft clipping any signal portion above the knee
		float scaled_knee = p_amp->knee * rail;
		float headroom = rail - scaled_knee;					// the rail "overage"
		float abs_sample = fabsf(sample);
		float over;

		if (abs_sample <= scaled_knee) {
			output = sample;
		}
		else {
			over = (abs_sample - scaled_knee) / headroom;
			output = copysignf(scaled_knee + headroom * tanhf(over), sample);
		}
		out_buf[i] = output;

		// Update the state of the load envelope
		//process_envelope_follower(&p_amp->e_fol, output);
		p_amp->power = p_amp->power + p_amp->power_coeff * (output * output - p_amp->power);
		process_envelope_follower(&p_amp->e_fol, p_amp->power);
		
		if (p_amp->e_fol.state < 1e-6f) {
			p_amp->e_fol.state = 0.0f;          /* avoid denormals */
		}
	}
}

void filter_cab_init(filter_cab* cab, int sample_rate) {

	// cab->input_hpf = bq_new(HIGHPASS, 80, BUTTERWORTH_Q, UNITY_GAIN, sample_rate);
	// cab->bass_res = bq_new(PEAK, 110, 1.2, 3, sample_rate);
	// cab->mid_res = bq_new(PEAK, 500, 0.8, -3, sample_rate);
	// cab->treble_res = bq_new(PEAK, 3000, 1.0, 5, sample_rate);
	// cab->output_lp1 = bq_new(LOWPASS, 5500, BUTTERWORTH_Q, UNITY_GAIN, sample_rate);
	// cab->output_lp2 = bq_new(LOWPASS, 5500, BUTTERWORTH_Q, UNITY_GAIN, sample_rate);

	cab->input_hpf     = bq_new(HIGHPASS, 95,   1.3,    UNITY_GAIN, sample_rate); // resonant: gives the low bump
	cab->mid_res       = bq_new(PEAK,     500,  0.8,    -3,         sample_rate);
	cab->treble_res    = bq_new(PEAK,     2800, 1.2,     5,         sample_rate);
	cab->breakup_notch = bq_new(PEAK,     4200, 4.0,    -4,         sample_rate); // replaces bass_res
	cab->output_lp1    = bq_new(LOWPASS,  5200, 0.5412, UNITY_GAIN, sample_rate);
	cab->output_lp2    = bq_new(LOWPASS,  5200, 1.3066, UNITY_GAIN, sample_rate);

}

void process_filter_cab(filter_cab* cab, float *in_buf, float *out_buf, int buffer_size) {

	float sample;
	for (int i = 0; i < buffer_size; i++) {

		sample = in_buf[i];
		sample = bq_process(cab->input_hpf, sample);
		// sample = bq_process(cab->bass_res, sample);
		sample = bq_process(cab->breakup_notch, sample);
		sample = bq_process(cab->mid_res, sample);
		sample = bq_process(cab->treble_res, sample);
		sample = bq_process(cab->output_lp1, sample);
		sample = bq_process(cab->output_lp2, sample);
		sample = soft_knee_distortion(sample);
		out_buf[i] = sample;
	}
}

void amp_model_init(amp_model* a_mod, int sample_rate, int buffer_size) {

	// set up base type
	effects_engine base;
	effects_engine_init(&base, "tube_amp");
	base.controls = combo_amp_controls;
	base.num_controls = COMBO_AMP_NUM_CONTROLS;
	base.colour = "#FFA500";

	// set up sub modules
	pre_amp pr_amp;
	pre_amp_init(&pr_amp, sample_rate, buffer_size);
	power_amp pw_amp;
	power_amp_init(&pw_amp, sample_rate);
	filter_cab cab;
	filter_cab_init(&cab, sample_rate);

	a_mod->base = base;
	a_mod->pr_amp = pr_amp;
	a_mod->pw_amp = pw_amp;
	a_mod->cab = cab;
	
	a_mod->sample_rate = sample_rate;

}

void process_amp_model(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size) {

	amp_model* a_mod = (amp_model*)engine;

	process_pre_amp(&a_mod->pr_amp, in_buf, out_buf, buffer_size);
	process_power_amp(&a_mod->pw_amp, out_buf, out_buf, buffer_size);
	process_filter_cab(&a_mod->cab, out_buf, out_buf, buffer_size);
}

void amp_head_init(amp_head* a_head, int sample_rate, int buffer_size) {

	effects_engine base;
	effects_engine_init(&base, "tube_amp_head");
	base.controls = amp_head_controls;
	base.num_controls = AMP_HEAD_NUM_CONTROLS;
	base.colour = "#FF8C00";

	pre_amp pr_amp;
	pre_amp_init(&pr_amp, sample_rate, buffer_size);

	a_head->base = base;
	a_head->pr_amp = pr_amp;
	a_head->sample_rate = sample_rate;
}

void process_amp_head(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size) {

	amp_head* a_head = (amp_head*)engine;

	process_pre_amp(&a_head->pr_amp, in_buf, out_buf, buffer_size);
}

void amp_cab_init(amp_cab* a_cab, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "tube_amp_cab");
	base.controls = NULL;
	base.num_controls = 0;
	base.colour = "#FF4500";

	power_amp pw_amp;
	power_amp_init(&pw_amp, sample_rate);
	filter_cab cab;
	filter_cab_init(&cab, sample_rate);

	a_cab->base = base;
	a_cab->pw_amp = pw_amp;
	a_cab->cab = cab;

}

void process_amp_cab(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size) {

	amp_cab* a_cab = (amp_cab*)engine;

	process_power_amp(&a_cab->pw_amp, in_buf, out_buf, buffer_size);
	process_filter_cab(&a_cab->cab, out_buf, out_buf, buffer_size);
}

// combo amp update functions
float set_preamp_gain(effects_engine* engine, float gain) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_gain = convert_to_range_exp(gain, minus_3dB, plus_21dB);
	a_mod->pr_amp.gain = converted_gain;

	return linear_gain_to_decibels(converted_gain);
}

float set_preamp_drive(effects_engine* engine, float drive) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_drive = convert_to_range_exp(drive, minus_3dB, plus_21dB);
	a_mod->pr_amp.drive = converted_drive;

	return linear_gain_to_decibels(converted_drive);
}

float set_preamp_level(effects_engine* engine, float level) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_level = convert_to_range_exp(level, MIN_PRAMP_LVL, MAX_PRAMP_LVL);
	a_mod->pr_amp.level = converted_level;

	return linear_gain_to_decibels(converted_level);
}

float set_preamp_bass(effects_engine* engine, float bass) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_bass = convert_to_range(bass, MIN_TS_GAIN, MAX_TS_GAIN);
	
	a_mod->pr_amp.tone.bass_gain = converted_bass;
	op_update(a_mod->pr_amp.tone.bass_lsf, OP_LOWSHELF, DFLT_BASS_FREQ, converted_bass, a_mod->pr_amp.tone.sample_rate);

	return converted_bass;
}

float set_preamp_mid(effects_engine* engine, float mid) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_mid = convert_to_range(mid, MIN_TS_GAIN, MAX_TS_GAIN);
	
	a_mod->pr_amp.tone.mid_gain = converted_mid;
	bq_update(a_mod->pr_amp.tone.mid_peak, PEAK, a_mod->pr_amp.tone.mid_freq, BUTTERWORTH_Q, converted_mid, a_mod->pr_amp.tone.sample_rate);

	return converted_mid;
}

float set_preamp_treble(effects_engine* engine, float treble) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_treble = convert_to_range(treble, MIN_TS_GAIN, MAX_TS_GAIN);
	
	a_mod->pr_amp.tone.treble_gain = converted_treble;
	op_update(a_mod->pr_amp.tone.treble_hsf, OP_HIGHSHELF, DFLT_TREBLE_FREQ, converted_treble, a_mod->pr_amp.tone.sample_rate);

	return converted_treble;
}

float set_preamp_mid_centre(effects_engine* engine, float centre) {

	amp_model* a_mod = (amp_model*)engine;

	float new_centre = centre * (MAX_MID_CENTRE- MIN_MID_CENTRE) + MIN_MID_CENTRE;
	a_mod->pr_amp.tone.mid_freq = new_centre;
	bq_update(a_mod->pr_amp.tone.mid_peak, PEAK, new_centre, BUTTERWORTH_Q, a_mod->pr_amp.tone.mid_gain, a_mod->pr_amp.tone.sample_rate);

	return new_centre;
}

// amp head update functions
float set_amp_head_gain(effects_engine* engine, float gain) {

	amp_head* a_mod = (amp_head*)engine;

	float converted_gain = convert_to_range_exp(gain, minus_3dB, plus_21dB);
	a_mod->pr_amp.gain = converted_gain;

	return linear_gain_to_decibels(converted_gain);
}

float set_amp_head_drive(effects_engine* engine, float drive) {

	amp_head* a_head = (amp_head*)engine;

	float converted_drive = convert_to_range_exp(drive, minus_3dB, plus_21dB);
	a_head->pr_amp.drive = converted_drive;

	return linear_gain_to_decibels(converted_drive);
}

float set_amp_head_level(effects_engine* engine, float level) {

	amp_head* a_head  = (amp_head*)engine;

	float converted_level = convert_to_range_exp(level, MIN_PRAMP_LVL, MAX_PRAMP_LVL);
	a_head->pr_amp.level = converted_level;

	return linear_gain_to_decibels(converted_level);
}

float set_amp_head_bass(effects_engine* engine, float bass) {

	amp_head* a_head  = (amp_head*)engine;

	float converted_bass = convert_to_range(bass, MIN_TS_GAIN, MAX_TS_GAIN);
	
	a_head->pr_amp.tone.bass_gain = converted_bass;
	op_update(a_head->pr_amp.tone.bass_lsf, OP_LOWSHELF, DFLT_BASS_FREQ, converted_bass, a_head->pr_amp.tone.sample_rate);

	return converted_bass;
}

float set_amp_head_mid(effects_engine* engine, float mid) {

	amp_head* a_head  = (amp_head*)engine;

	float converted_mid = convert_to_range(mid, MIN_TS_GAIN, MAX_TS_GAIN);
	
	a_head->pr_amp.tone.mid_gain = converted_mid;
	bq_update(a_head->pr_amp.tone.mid_peak, PEAK, a_head->pr_amp.tone.mid_freq, BUTTERWORTH_Q, converted_mid, a_head->pr_amp.tone.sample_rate);

	return converted_mid;
}

float set_amp_head_treble(effects_engine* engine, float treble) {

	amp_head* a_head  = (amp_head*)engine;

	float converted_treble = convert_to_range(treble, MIN_TS_GAIN, MAX_TS_GAIN);
	
	a_head->pr_amp.tone.treble_gain = converted_treble;
	op_update(a_head->pr_amp.tone.treble_hsf, OP_HIGHSHELF, DFLT_TREBLE_FREQ, converted_treble, a_head->pr_amp.tone.sample_rate);

	return converted_treble;
}

float set_amp_head_mid_centre(effects_engine* engine, float centre) {

	amp_head* a_head  = (amp_head*)engine;

	float new_centre = centre * (MAX_MID_CENTRE- MIN_MID_CENTRE) + MIN_MID_CENTRE;
	a_head->pr_amp.tone.mid_freq = new_centre;
	bq_update(a_head->pr_amp.tone.mid_peak, PEAK, new_centre, BUTTERWORTH_Q, a_head->pr_amp.tone.mid_gain, a_head->pr_amp.tone.sample_rate);

	return new_centre;
}
