#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "dsp_tools.h"
#include "compressor_effects.h"

const ControlDef compressor_controls[COMP_NUM_CONTROLS] = {
	[COMP_CTRL_ATCK] = { .name = "cmp_attack", .label = "Attack", .type = CTRL_KNOB, .row = COMPRESSOR_ROW_NUM, .default_value = DFLT_COMP_ATCK_SET, .update_func = set_comp_attack, .units = "ms" },
	[COMP_CTRL_RLSE] = { .name = "cmp_release", .label = "Release", .type = CTRL_KNOB, .row = COMPRESSOR_ROW_NUM, .default_value = DFLT_COMP_RLSE_SET, .update_func = set_comp_release, .units = "ms"  },
	[COMP_CTRL_MKGAIN] = { .name = "cmp_mk_gain", .label = "Make-up Gain", .type = CTRL_KNOB, .row = COMPRESSOR_ROW_NUM, .default_value = DFLT_COMP_MKGAIN_SET, .update_func = set_comp_mkp_gain, .units = "dB"  },
	[COMP_CTRL_THRSH] = { .name = "cmp_threshold", .label = "Threshold", .type = CTRL_KNOB, .row = COMPRESSOR_ROW_NUM, .default_value = DFLT_COMP_MKGAIN_SET, .update_func = set_comp_threshold, .units = "dB" },
	[COMP_CTRL_RATIO] = { .name = "cmp_ratio", .label = "Ratio", .type = CTRL_KNOB, .row = COMPRESSOR_ROW_NUM, .default_value = DFLT_COMP_RATIO_SET, .update_func = set_comp_ratio, .units = "" },
	[COMP_CTRL_LVL] = { .name = "cmp_level", .label = "Level", .type = CTRL_KNOB, .row = COMPRESSOR_ROW_NUM, .default_value = DFLT_COMP_LVL_SET, .update_func = set_comp_level, .units = "dB" },
	[COMP_CTRL_BYPASS] = { .name = "cmp_bypass", .label = "Bypass", .type = CTRL_SWITCH, .row = COMPRESSOR_ROW_NUM, .default_value = OFF, .update_func = set_comp_bypass, .units = "" }
};

void compressor_init(compressor_engine* c_engine, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "compressor");
	base.controls = compressor_controls;
	base.num_controls = COMP_NUM_CONTROLS;
	base.colour = "#C0C0C0";
	c_engine->base = base;

	// envelope_follower_init(&c_engine->e_fol, DFLT_COMP_ATCK, DFLT_COMP_RLSE, sample_rate);

	c_engine->sample_rate = sample_rate;
	set_comp_attack(&c_engine->base, DFLT_COMP_ATCK_SET);
	set_comp_release(&c_engine->base, DFLT_COMP_RLSE_SET);
	set_comp_threshold(&c_engine->base, DFLT_COMP_THRSH_SET);
	set_comp_mkp_gain(&c_engine->base, DFLT_COMP_MKGAIN_SET);
	set_comp_ratio(&c_engine->base, DFLT_COMP_RATIO_SET);
	set_comp_level(&c_engine->base, DFLT_COMP_LVL_SET);
	c_engine->bypass = OFF;
}

void process_compressor(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size) {
	
	compressor_engine* c_engine = (compressor_engine*)engine;

	if (c_engine->bypass == 0) {
		return;
	}

	float level_dB, over_dB, target_dB, released, gain, output;

	for(int i = 0; i < buffer_size; i++) {

		// calculate the current level, the overage, and the target gain after compression
		level_dB = 20.0f * log10f( fabsf( in_buf[i] + 1e-9f) );									// add 0.000000001, to avoid infinity
		over_dB = level_dB  - c_engine->threshold_dB;
		if (over_dB > 0.0f) {
			target_dB = over_dB * c_engine->exponent;
		}
		else {
			target_dB = 0.0f;
		}

		// Catch peaks, and handle release
		released = c_engine->release_alpha * c_engine->peak_dB + (1.0f - c_engine->release_alpha) * target_dB;
		c_engine->peak_dB = fmaxf(target_dB, released);

		// attack smoothing
		c_engine->reduction_dB = c_engine->attack_alpha * c_engine->reduction_dB + (1.0f - c_engine->attack_alpha) * c_engine->peak_dB;

		// Apply compression and make up gain
		gain = powf(10.0f, -(c_engine->reduction_dB / 20.0f) ) * c_engine->makeup_gain;
		output = in_buf[i] * gain;
		out_buf[i] = output * c_engine->level;

	}
	
}

float set_comp_attack(effects_engine* engine, float attack) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float new_attack = linear_to_log(attack, MIN_COMP_ATCK, MAX_COMP_ATCK);
	c_engine->attack_ms = new_attack;
	c_engine->attack_alpha = expf(-1.0f / ( new_attack * (0.001f * c_engine->sample_rate)) );

	return new_attack;
}

float set_comp_release(effects_engine* engine, float release) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float new_release = linear_to_log(release, MIN_COMP_RLSE, MAX_COMP_RLSE);
	c_engine->release_ms = new_release;
	c_engine->release_alpha = expf(-1.0f / ( new_release * (0.001f * c_engine->sample_rate)) );

	return new_release;
}

float set_comp_mkp_gain(effects_engine* engine, float gain) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float new_gain = linear_to_log(gain, MIN_COMP_MKGAIN, MAX_COMP_MKGAIN);	//convert the linear range 0-1 to the logarithmic range 1-5.6234
	c_engine->makeup_gain = new_gain;

	return linear_gain_to_decibels(new_gain);
}

float set_comp_threshold(effects_engine* engine, float threshold) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	// float new_threshold = linear_to_log(threshold, MIN_COMP_THRSH, MAX_COMP_THRSH);
	// c_engine->threshold = new_threshold;
	float new_threshold = convert_to_range(threshold, MIN_COMP_THRSH, MAX_COMP_THRSH);
	c_engine->threshold_dB = new_threshold;

	// return linear_gain_to_decibels(new_threshold);
	return new_threshold;
}

float set_comp_ratio(effects_engine* engine, float ratio) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float new_ratio = linear_to_log(ratio, MIN_COMP_RATIO, MAX_COMP_RATIO);		// converts linear 0-1 to logarithmic 1-20
	float new_exponent = 1.0f - 1.0f / new_ratio;								// If I want a limiter later, setting the exponent to exactly 1.0f would do that
	c_engine->ratio = new_ratio;
	c_engine->exponent = new_exponent;

	return new_ratio;
}

float set_comp_level(effects_engine* engine, float ratio) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	float new_level = linear_to_log(ratio, MIN_COMP_LVL, MAX_COMP_LVL);
	c_engine->level = new_level;

	return linear_gain_to_decibels(new_level);
}

float set_comp_bypass(effects_engine* engine, float bypass) {

	compressor_engine* c_engine = (compressor_engine*)engine;

	int new_bypass = (int)bypass;
	c_engine->bypass = new_bypass;

	return new_bypass;
}
