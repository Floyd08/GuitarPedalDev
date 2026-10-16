#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "dsp_tools.h"
#include "compressor_effects.h"

const ControlDef compressor_controls[COMP_NUM_CONTROLS] = {
	[COMP_CTRL_ATCK] = { .name = "attack", .label = "Attack", .type = CTRL_KNOB, .row = 3, .default_value = DFLT_ATCK, .update_func = set_comp_attack },
	[COMP_CTRL_RLSE] = { .name = "release", .label = "Release", .type = CTRL_KNOB, .row = 3, .default_value = DFLT_RLSE, .update_func = set_comp_release },
	[COMP_CTRL_THRSH] = { .name = "makeup_gain", .label = "Make-up Gain", .type = CTRL_KNOB, .row = 3, .default_value = DFLT_MKGAIN, .update_func = set_threshold },
	[COMP_CTRL_MKGAIN] = { .name = "threshold", .label = "Threshold", .type = CTRL_KNOB, .row = 3, .default_value = DFLT_THRSH, .update_func = set_makeup_gain },
	[COMP_CTRL_RATIO] = { .name = "ratio", .label = "Ratio", .type = CTRL_KNOB, .row = 3, .default_value = DFLT_RATIO, .update_func = set_ratio },
};

void compressor_init(compressor_engine* c_engine, envelope_follower* e_fol, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "compressor");
	base.controls = compressor_controls;
	base.num_controls = COMP_NUM_CONTROLS;

	c_engine->base = base;
	c_engine->sample_rate = sample_rate;
	c_engine->e_fol = e_fol;
	set_comp_attack(&c_engine->base, DFLT_ATCK);
	set_comp_release(&c_engine->base, DFLT_RLSE);
	set_threshold(&c_engine->base, DFLT_THRSH);
	set_makeup_gain(&c_engine->base, DFLT_MKGAIN);
	set_ratio(&c_engine->base, DFLT_RATIO);
}

void process_compressor(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size) {
	
	compressor_engine* c_engine = (compressor_engine*)engine;

	float gain_scale;

	for(int i = 0; i < buffer_size; i++) {

		gain_scale = 1.0f;
		process_envelope_follower(c_engine->e_fol, in_buf[i]);										//update the state of the envelope with this sample
		
		if (c_engine->e_fol->state > c_engine->threshold) {
			gain_scale = powf(c_engine->threshold / c_engine->e_fol->state, c_engine->exponent);
		}
		out_buf[i] = in_buf[i] * gain_scale * c_engine->makeup_gain;
		out_buf[i] = tanhf(out_buf[i]);																//prevents make up gain from driving the signal too hard
	}
}

void set_comp_attack(effects_engine* engine, float attack) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float attack_log = linear_to_log(attack, COMP_ATCK_MIN, COMP_ATCK_MAX);
	c_engine->attack = attack_log;
	set_env_attack(c_engine->e_fol, attack_log, c_engine->sample_rate);
}

void set_comp_release(effects_engine* engine, float release) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float release_log = linear_to_log(release, COMP_RLSE_MIN, COMP_RLSE_MAX);
	c_engine->release = release_log;
	set_env_release(c_engine->e_fol, release_log, c_engine->sample_rate);
}

void set_makeup_gain(effects_engine* engine, float gain) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float new_gain = linear_to_log(gain, _0dB, plus_15dB);					//convert the linear range 0-1 to the logarithmic range 1-5.6234
	c_engine->makeup_gain = new_gain;
}

void set_threshold(effects_engine* engine, float threshold) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float new_thresh_dB = -40.0f + 40.0f * threshold; 						// turning the knob clockwise raises the threshold and lessens compression
	float new_thresh_lin = powf(10.0f, new_thresh_dB / 20.0f);				// convert to a linear range
	c_engine->threshold = new_thresh_lin;
}

void set_ratio(effects_engine* engine, float ratio) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float new_ratio = powf(20.0f, ratio);									// converts linear 0-1 to logarithmic 1-20
	float new_exponent = 1.0f - 1.0f / new_ratio;							// If I want a limiter later, setting the exponent to exactly 1.0f would do that
	c_engine->ratio = new_ratio;
	c_engine->exponent = new_exponent;
}
