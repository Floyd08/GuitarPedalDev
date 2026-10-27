#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "amp_model.h"

const ControlDef amp_model_controls[AMP_NUM_CONTROLS] = {
	[AMP_CTRL_GAIN] = { .name = "amp_gain", .label = "Gain", .type = CTRL_KNOB, .row = 4, .default_value = 0.17f, .update_func = set_preamp_gain },
	[AMP_CTRL_BASS] = { .name = "amp_bass", .label = "Bass", .type = CTRL_KNOB, .row = 4, .default_value = DFLT_GAIN_LINEAR, .update_func = set_preamp_bass },
	[AMP_CTRL_MID] = { .name = "amp_mid", .label = "Mid", .type = CTRL_KNOB, .row = 4, .default_value = DFLT_GAIN_LINEAR, .update_func = set_preamp_mid },
	[AMP_CTRL_TRBL] = { .name = "amp_treble", .label = "Treble", .type = CTRL_KNOB, .row = 4, .default_value = DFLT_GAIN_LINEAR, .update_func = set_preamp_treble },
	[AMP_CTRL_MID_SWP] = { .name = "amp_mid_swp", .label = "Mid Sweep", .type = CTRL_KNOB, .row = 4, .default_value = 0.2, .update_func = set_preamp_mid_centre },
	[AMP_CTRL_DRVE] = { .name = "amp_drive", .label = "Drive", .type = CTRL_KNOB, .row = 4, .default_value = 0.17f, .update_func = set_preamp_drive}
};

void tone_stack_init(tone_stack* tone, int sample_rate) {
	
	tone->bass_lsf = op_new(LOWSHELF, DFLT_BASS_FREQ, DFLT_GAIN_dB, sample_rate);
	tone->bass_gain = DFLT_GAIN_LINEAR;

	tone->mid_peak = op_new(PEAK, DFLT_MID_FREQ, DFLT_GAIN_dB, sample_rate);
	tone->mid_gain = DFLT_GAIN_LINEAR;
	tone->mid_freq = DFLT_MID_FREQ_LN;

	tone->treble_hsf = op_new(HIGHSHELF, DFLT_TREBLE_FREQ, DFLT_GAIN_dB, sample_rate);
	tone->treble_gain = DFLT_GAIN_LINEAR;

}

float process_tone_stack(tone_stack* tone, float sample) {

	sample = op_process(tone->bass_lsf, sample);
	sample = op_process(tone->treble_hsf, sample);
	sample = op_process(tone->mid_peak, sample);
	return sample;	
}

void pre_amp_init(pre_amp* pre_amp, int sample_rate, int buffer_size) {

	int oversample_rate = sample_rate * OVERSAMPLING_FACTOR;

	tone_stack_init(&pre_amp->tone, oversample_rate);
	pre_amp->gain = _0dB;
	pre_amp->drive = _0dB;
	pre_amp->input_hpf = op_new(HIGHPASS, 60, UNITY_GAIN, oversample_rate);
	pre_amp->interstage_hpf = op_new(HIGHPASS, 150, UNITY_GAIN, oversample_rate);
	pre_amp->interstage_lpf = op_new(LOWPASS, 7000, UNITY_GAIN, oversample_rate);
	pre_amp->bias = 0.15f;
	pre_amp->bias_precomp = tanhf(pre_amp->bias);
	
	pre_amp->oversampling_buffer = calloc( buffer_size * OVERSAMPLING_FACTOR, sizeof(float) );
	pre_amp->image_filter = alias_init(OVERSAMPLING_FACTOR, oversample_rate);
	pre_amp->alias_filter = alias_init(OVERSAMPLING_FACTOR, oversample_rate);
}

void process_pre_amp(pre_amp* pre_amp, float *in_buf, float *out_buf, int buffer_size) {

	float sample;

	upsample(in_buf, buffer_size, pre_amp->oversampling_buffer, OVERSAMPLING_FACTOR);
	int oversampling_buffer_length = buffer_size * OVERSAMPLING_FACTOR;

	for (int i = 0; i < oversampling_buffer_length; i++) {

		sample = pre_amp->oversampling_buffer[i];
		sample = bq_process(pre_amp->image_filter, sample);
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

		sample = bq_process(pre_amp->alias_filter, sample);
		pre_amp->oversampling_buffer[i] = sample;
	}

	downsample_mono(pre_amp->oversampling_buffer, OVERSAMPLING_FACTOR, out_buf, buffer_size);
}

void power_amp_init(power_amp* p_amp, int sample_rate) {

	envelope_follower_init(&p_amp->e_fol, POWER_AMP_ATTACK_MS, POWER_AMP_RELEASE_MS, sample_rate);

	p_amp->rail_depth = RAIL_DEPTH;
	p_amp->knee = RAIL_KNEE;
	p_amp->rail_floor = RAIL_FLOOR;

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
		process_envelope_follower(&p_amp->e_fol, output);
		
		if (p_amp->e_fol.state < 1e-6f) {
			p_amp->e_fol.state = 0.0f;          /* avoid denormals */
		}
	}
}

void filter_cab_init(filter_cab* cab, int sample_rate) {

	cab->input_hpf = bq_new(HIGHPASS, 80, BUTTERWORTH_Q, UNITY_GAIN, sample_rate);
	cab->bass_res = bq_new(PEAK, 110, 1.2, 3, sample_rate);
	cab->mid_res = bq_new(PEAK, 500, 0.8, -3, sample_rate);
	cab->treble_res = bq_new(PEAK, 3000, 1.0, 5, sample_rate);
	cab->output_lp1 = bq_new(LOWPASS, 5500, BUTTERWORTH_Q, UNITY_GAIN, sample_rate);
	cab->output_lp2 = bq_new(LOWPASS, 5500, BUTTERWORTH_Q, UNITY_GAIN, sample_rate);

}

void process_filter_cab(filter_cab* cab, float *in_buf, float *out_buf, int buffer_size) {

	float sample;
	for (int i = 0; i < buffer_size; i++) {

		sample = in_buf[i];
		sample = bq_process(cab->bass_res, sample);
		sample = bq_process(cab->bass_res, sample);
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
	base.controls = amp_model_controls;
	base.num_controls = AMP_NUM_CONTROLS;

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

// update functions
void set_preamp_gain(effects_engine* engine, float gain) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_gain = linear_to_log(gain, minus_3dB, plus_21dB);
	a_mod->pr_amp.gain = converted_gain;
}

void set_preamp_drive(effects_engine* engine, float gain) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_gain = linear_to_log(gain, minus_3dB, plus_21dB);
	a_mod->pr_amp.drive = converted_gain;
}

void set_preamp_bass(effects_engine* engine, float bass) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_bass = linear_to_log(bass, MIN_TS_GAIN, MAX_TS_GAIN);
	a_mod->pr_amp.tone.bass_gain = converted_bass;
}

void set_preamp_mid(effects_engine* engine, float mid) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_mid = linear_to_log(mid, MIN_TS_GAIN, MAX_TS_GAIN);
	a_mod->pr_amp.tone.mid_gain = converted_mid;
}

void set_preamp_treble(effects_engine* engine, float treble) {

	amp_model* a_mod = (amp_model*)engine;

	float converted_treble = linear_to_log(treble, MIN_TS_GAIN, MAX_TS_GAIN);
	a_mod->pr_amp.tone.treble_gain = converted_treble;
}

void set_preamp_mid_centre(effects_engine* engine, float centre) {

	amp_model* a_mod = (amp_model*)engine;

	float new_centre = centre * (MAX_MID_CENTRE- MIN_MID_CENTRE) + MIN_MID_CENTRE;
	a_mod->pr_amp.tone.mid_freq = new_centre;
}
