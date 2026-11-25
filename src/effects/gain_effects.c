#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "dsp_tools.h"
#include "Biquad.h"
#include "control_def.h"
#include "effects_engine.h"
#include "gain_effects.h"

//The controls this module offers to the UI. ui_listener registers this table, and the UI builds itself from it
//Rows are relative to this module (0 is its first row). Names must be unique across ALL modules
const ControlDef distortion_controls[DIST_NUM_CONTROLS] = {
	[DIST_CTRL_GAIN] = { .name = "gain", .label = "Gain", .type = CTRL_KNOB, .row = DISTORTION_ROW_NUM, .default_value = 0.10f, .update_func = set_distortion_gain, .units = "dB" },
	[DIST_CTRL_BIAS] = { .name = "bias", .label = "Bias", .type = CTRL_KNOB, .row = DISTORTION_ROW_NUM, .default_value = 0.625f, .update_func = set_distortion_bias , .units = "" },
	[DIST_CTRL_WET] = { .name = "dstr_wet", .label = "Dry/Wet", .type = CTRL_KNOB, .row = DISTORTION_ROW_NUM, .default_value = 0.5f, .update_func = set_distortion_mix, .units = "" },
	[DIST_CTRL_BYPS] = { .name = "dstr_bypass", .label = "Bypass", .type = CTRL_SWITCH, .row = DISTORTION_ROW_NUM, .default_value = OFF, .update_func = set_distortion_bypass, .units = "" }
};

//initialization. Should handle all instantiation for the distortion engine, so everything is ready for runtime
void gain_init(gain_engine* g_engine, /*gain_stage** g_stages,*/ int num_stages, int up_factor, int sample_rate, int buffer_size) {

	effects_engine base;
	effects_engine_init(&base, "distortion");
	base.controls = distortion_controls;
	base.num_controls = DIST_NUM_CONTROLS;
	base.colour = "#800000";
	g_engine->base = base;
	
	g_engine->image_filter = alias_init(up_factor, sample_rate);
	g_engine->alias_filter = alias_init(up_factor, sample_rate);

	if (num_stages > MAX_GAIN_STAGES) {
		fprintf(stderr, "num stages >= MAX STAGES\n");
	}
	for (int i = 0; i < num_stages; i++) {
		//g_engine->g_stages[i] = g_stages[i];
		g_engine->g_stages[i] = &g_engine->slots[i].base;
	}

	g_engine->num_stages = num_stages;
	g_engine->up_factor = up_factor;
	g_engine->buffer_size = buffer_size;
	g_engine->oversampling_buffer = calloc( buffer_size * up_factor, sizeof(float) );
	g_engine->bypass = OFF;
}

void general_stage_init(general_gain_stage* g_stage, biquad** pre_filters, int num_pre_filters, float rectification, float bias, float gain, float dstr_wet, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters) {

	g_stage->base.type = GENERAL_STAGE;
	g_stage->base.process = process_general_stage;
	g_stage->base.free = general_stage_free;

	if (num_pre_filters > MAX_STAGE_PREFILTERS) {
		fprintf(stderr, "num prefilters >= MAX PREFILTERS\n");
	}
	g_stage->num_pre_filters = num_pre_filters;
	for (int i = 0; i < num_pre_filters; i++) {
		g_stage->pre_filters[i] = pre_filters[i];
	}

	g_stage->rectification = rectification;
	g_stage->bias = bias;
	g_stage->gain = gain;
	g_stage->dstr_wet = dstr_wet;
	g_stage->d_effect = d_effect;
	
	// g_stage->num_post_filters = num_post_filters;
	// g_stage->post_filters = post_filters;
	if (num_post_filters > MAX_STAGE_POSTFILTERS) {
		fprintf(stderr, "num postfilters >= MAX POSTFILTERS\n");
	}
	g_stage->num_post_filters = num_post_filters;
	for (int i = 0; i < num_post_filters; i++) {
		g_stage->post_filters[i] = post_filters[i];
	}
}

void clean_stage_init(clean_gain_stage* g_stage, biquad** pre_filters, int num_pre_filters, float gain, biquad** post_filters, int num_post_filters) {

	g_stage->base.type = CLEAN_STAGE;
	g_stage->base.process = process_clean_stage;
	g_stage->base.free = clean_stage_free;

	if (num_pre_filters > MAX_STAGE_PREFILTERS) {
		fprintf(stderr, "num prefilters >= MAX PREFILTERS\n");
	}
	g_stage->num_pre_filters = num_pre_filters;
	for (int i = 0; i < num_pre_filters; i++) {
		g_stage->pre_filters[i] = pre_filters[i];
	}
	
	g_stage->gain = gain;

	if (num_post_filters > MAX_STAGE_POSTFILTERS) {
		fprintf(stderr, "num postfilters >= MAX POSTFILTERS\n");
	}
	g_stage->num_post_filters = num_post_filters;
	for (int i = 0; i < num_post_filters; i++) {
		g_stage->post_filters[i] = post_filters[i];
	}

}

void distortion_stage_init(distortion_gain_stage* g_stage, biquad** pre_filters, int num_pre_filters, float gain, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters) {

	g_stage->base.type = DISTORTION_STAGE;
	g_stage->base.process = process_distortion_stage;
	g_stage->base.free = distortion_stage_free;

	if (num_pre_filters > MAX_STAGE_PREFILTERS) {
		fprintf(stderr, "num prefilters >= MAX PREFILTERS\n");
	}
	g_stage->num_pre_filters = num_pre_filters;
	for (int i = 0; i < num_pre_filters; i++) {
		g_stage->pre_filters[i] = pre_filters[i];
	}

	g_stage->gain = gain;
	g_stage->d_effect = d_effect;
	
	if (num_post_filters > MAX_STAGE_POSTFILTERS) {
		fprintf(stderr, "num postfilters >= MAX POSTFILTERS\n");
	}
	g_stage->num_post_filters = num_post_filters;
	for (int i = 0; i < num_post_filters; i++) {
		g_stage->post_filters[i] = post_filters[i];
	}

}

void biased_stage_init(biased_gain_stage* g_stage, biquad** pre_filters, int num_pre_filters, float bias, float gain, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters) {

	g_stage->base.type = BIASED_STAGE;
	g_stage->base.process = process_biased_stage;
	g_stage->base.free = biased_stage_free;

	if (num_pre_filters > MAX_STAGE_PREFILTERS) {
		fprintf(stderr, "num prefilters >= MAX PREFILTERS\n");
	}
	g_stage->num_pre_filters = num_pre_filters;
	for (int i = 0; i < num_pre_filters; i++) {
		g_stage->pre_filters[i] = pre_filters[i];
	}

	g_stage->bias = bias;
	g_stage->gain = gain;
	g_stage->d_effect = d_effect;
	
	if (num_post_filters > MAX_STAGE_POSTFILTERS) {
		fprintf(stderr, "num postfilters >= MAX POSTFILTERS\n");
	}
	g_stage->num_post_filters = num_post_filters;
	for (int i = 0; i < num_post_filters; i++) {
		g_stage->post_filters[i] = post_filters[i];
	}
}

void process_gain(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size) {

	gain_engine* g_engine = (gain_engine*)engine;

	if (g_engine->bypass == 0) {
		return;
	}

	float sample, dry_sig, damp_sig;

	if (g_engine->up_factor < 2) {

		for(int i = 0, j = 0; i < g_engine->buffer_size; i++) {
			
			sample = in_buf[i];
			for (int n = 0; n < g_engine->num_stages; n++) {
				//call the nth gain_stage to process the ith sample
				// sample = process_gain_stage(g_engine, g_engine->g_stages[n], sample);
				sample = g_engine->g_stages[n]->process(g_engine->g_stages[n], g_engine, sample);
			}
			out_buf[j++] = sample;
		}
	}
	else {
		
		upsample(in_buf, buffer_size, g_engine->oversampling_buffer, g_engine->up_factor);				// zero stuffing increases the sample rate
		
		int oversampling_buffer_length = buffer_size * g_engine->up_factor;
		for(int i = 0, j = 0; i < oversampling_buffer_length; i++) {
			
			sample = g_engine->oversampling_buffer[i];

			sample = bq_process(g_engine->image_filter, sample);							// anti-imaging filter smooths the signal
			sample  = sample * g_engine->up_factor;											// compensate for gain loss
			
			for (int n = 0; n < g_engine->num_stages; n++) {
				//call the nth gain_stage to process the ith sample
				//printf("processing stage: %d, on sample: %f\n", n, sample);
				if (g_engine->g_stages[n] == NULL) {
					fprintf(stderr, "gain stage %d is NULL\n", n);
				}
				// sample = process_gain_stage(g_engine, g_engine->g_stages[n], sample);
				sample = g_engine->g_stages[n]->process(g_engine->g_stages[n], g_engine, sample);
			}
			sample = bq_process(g_engine->alias_filter, sample);							// anti-aliasing filter
			g_engine->oversampling_buffer[j++] = sample;
		}

		downsample_mono(g_engine->oversampling_buffer, g_engine->up_factor, out_buf, buffer_size);		// restore to the original sample rate
		// Dry/Wet blend
	}
}

float process_general_stage(gain_stage* stage, gain_engine* g_engine, float sample) {

	general_gain_stage* g_stage = (general_gain_stage*)stage;
	(void)g_engine;
	float output = sample;
	
	//apply gain
	output = output * g_stage->gain;

	//rectify, if desired
	if (g_stage->rectification == 1) {
		output = soft_rectify(output, 0.8);
	}
	
	//loop through the pre distortion filters
	for (int i = 0; i < g_stage->num_pre_filters; i++) {
		output = bq_process(g_stage->pre_filters[i], output);
	}
	
	//apply bias
	output = output + g_stage->bias;
	
	//apply the waveshaping function and remove the bias
	output = g_stage->d_effect(output) - g_stage->d_effect(g_stage->bias);
	
	//loop through the post distortion filters
	for (int i = 0; i < g_stage->num_post_filters; i++) {
		output = bq_process(g_stage->post_filters[i], output);
	}
	
	if (g_stage->dstr_wet != -1) {
		output = output * (g_stage->dstr_wet) + output * (1.0f - g_stage->dstr_wet);
	}
	
	return output;
}

float process_clean_stage(gain_stage* stage, gain_engine* g_engine, float sample) {

	clean_gain_stage* g_stage = (clean_gain_stage*)stage;
	(void)g_engine;
	float output = sample;

	//loop through the pre distortion filters
	for (int i = 0; i < g_stage->num_pre_filters; i++) {
		output = bq_process(g_stage->pre_filters[i], output);
	}

	//apply gain
	output = output * g_stage->gain;

	//loop through the post distortion filters
	for (int i = 0; i < g_stage->num_post_filters; i++) {
		output = bq_process(g_stage->post_filters[i], output);
	}

	return output;
}

float process_distortion_stage(gain_stage* stage, gain_engine* g_engine, float sample) {

	distortion_gain_stage* g_stage = (distortion_gain_stage*)stage;
	(void)g_engine;
	float output = sample;
	
	//apply gain
	output = output * g_stage->gain;
	
	//loop through the pre distortion filters
	for (int i = 0; i < g_stage->num_pre_filters; i++) {
		output = bq_process(g_stage->pre_filters[i], output);
	}
	
	//apply the waveshaping function
	output = g_stage->d_effect(output);
	
	//loop through the post distortion filters
	for (int i = 0; i < g_stage->num_post_filters; i++) {
		output = bq_process(g_stage->post_filters[i], output);
	}
	
	return output;
}

float process_biased_stage(gain_stage* stage, gain_engine* g_engine, float sample) {

	biased_gain_stage* g_stage = (biased_gain_stage*)stage;
	(void)g_engine;
	float output = sample;
	
	//apply gain
	output = output * g_stage->gain;
	
	//loop through the pre distortion filters
	for (int i = 0; i < g_stage->num_pre_filters; i++) {
		output = bq_process(g_stage->pre_filters[i], output);
	}
	
	//apply bias
	output = output + g_stage->bias;
	
	//apply the waveshaping function and remove the bias
	output = g_stage->d_effect(output) - g_stage->d_effect(g_stage->bias);
	
	//loop through the post distortion filters
	for (int i = 0; i < g_stage->num_post_filters; i++) {
		output = bq_process(g_stage->post_filters[i], output);
	}
	
	return output;
}


void gain_free(gain_engine* g_engine) {

	bq_destroy(g_engine->alias_filter);

	for (int i = 0; i < g_engine->num_stages; i++) {
		gain_stage_free(g_engine->g_stages[i]);
	}
	
}

void gain_stage_free(gain_stage* g_stage) {

	g_stage->free(g_stage);
}

void general_stage_free(gain_stage* stage) {

	general_gain_stage* g_stage = (general_gain_stage*) stage;

	for (int i = 0; i < g_stage->num_pre_filters; i++) {
		bq_destroy(g_stage->pre_filters[i]);
	}

	for (int i = 0; i < g_stage->num_post_filters; i++) {
		bq_destroy(g_stage->post_filters[i]);
	}

}

void clean_stage_free(gain_stage* stage) {

	clean_gain_stage* g_stage = (clean_gain_stage*) stage;

	for (int i = 0; i < g_stage->num_pre_filters; i++) {
		bq_destroy(g_stage->pre_filters[i]);
	}

	for (int i = 0; i < g_stage->num_post_filters; i++) {
		bq_destroy(g_stage->post_filters[i]);
	}

}

void distortion_stage_free(gain_stage* stage) {

	distortion_gain_stage* g_stage = (distortion_gain_stage*) stage;

	for (int i = 0; i < g_stage->num_pre_filters; i++) {
		bq_destroy(g_stage->pre_filters[i]);
	}

	for (int i = 0; i < g_stage->num_post_filters; i++) {
		bq_destroy(g_stage->post_filters[i]);
	}

}

void biased_stage_free(gain_stage* stage) {

	biased_gain_stage* g_stage = (biased_gain_stage*) stage;

	for (int i = 0; i < g_stage->num_pre_filters; i++) {
		bq_destroy(g_stage->pre_filters[i]);
	}

	for (int i = 0; i < g_stage->num_post_filters; i++) {
		bq_destroy(g_stage->post_filters[i]);
	}

}


// Update functions

// fav distortion 1
float set_distortion_gain (effects_engine* engine, float gain) {

	gain_engine* g_engine = (gain_engine*)engine;

	//float converted_gain = expf(gain * 3.41);
	float converted_gain = convert_to_range_exp(gain, minus_3dB, plus_30dB);
	//g_engine->g_stages[2]-> = converted_gain;
	g_engine->slots[1].bias.gain = converted_gain;

	return linear_gain_to_decibels(converted_gain);
}

float set_distortion_bias (effects_engine* engine, float bias) {

	gain_engine* g_engine = (gain_engine*)engine;

	// value * (new_max - new_min) + new_min		->		max 1 and min -1, gets us to 2 -1
	float new_bias = (bias * 2) - 1;				
	// int stage_index = g_engine->control_stages[1];
	// g_engine->g_stages[stage_index]->bias = new_bias;
	g_engine->slots[1].bias.bias = new_bias;

	return new_bias;
}

float set_distortion_mix (effects_engine* engine, float wet) {

	gain_engine* g_engine = (gain_engine*)engine;
	
	// int stage_index = g_engine->control_stages[2];
	// g_engine->g_stages[stage_index]->dstr_wet = wet;
	// g_engine->slots[1].general.dstr_wet = wet;

	return wet;
}

float set_distortion_bypass(effects_engine* engine, float bypass) {

	gain_engine* g_engine = (gain_engine*)engine;

	int new_bypass = (int)bypass;
	// g_engine->bypass = new_bypass;
	g_engine->bypass = new_bypass;

	return new_bypass;
}


//Wave shaping functions, processed sample-by-sample
float soft_knee_distortion(float sample) {
	
	float over = 0.0f, knee = 0.8f;						// a high knee clips less harshly, and creates a soft distortion curve
														// consider adding knee and it's reciprocal to the distortion engine, so we don't have to compute per sample
	float abv_sample = fabsf(sample);

	if (abv_sample <= knee) {							// below the knee, just pass the sample through
		return sample;
	}
	over = (abv_sample - knee) / (1.0f - knee);			// find out by how much we've gone over the knee
	abv_sample = knee + (1.0f - knee) * tanhf(over);	// clip the overage, then scale it back down to add to the portion below the knee
	sample = copysignf(abv_sample, sample);				// restore the sign of the signal
	return sample;
}

float tanh_distortion(float sample) {
	return tanhf(sample);
}

float distortion_passthrough (float sample) {
	return sample;
}

float bitcrush_distortion(float sample) {

	int bit_depth = 4;
	float levels = powf(2.0f, bit_depth);				//2^bit_depth;
	float scale = levels / 2;
	// float scale = (float)(1 << (bit_depth - 1));

	sample = fmaxf(-1.0f, fminf(1.0f, sample));			// clamp to {-1, 1}
	sample = sample * scale;
	sample = roundf(sample);
	sample = sample / scale;

	return sample;
}


float min_max_fuzz(float sample) {
	
	sample = fminf(fmaxf(sample, -1), 1);
	return sample;
}

float wavefolder_1(float sample) {

	// sample = sample * M_2_PI;
	sample = sample * M_PI_2;
	sample  = sinf(sample);
	return sample;
}

float wavefolder_2(float sample) {

	sample = wavefolder_1(sample);
	sample = asinf(sample);
	// sample = sample * M_2_PI;
	sample = sample * M_PI_2;
	return sample;
}

// rectifiers
float rectify(float sample) {

	return fabsf(sample);
}
// A softer rectification
float soft_rectify(float sample, float knee) {

	float rect = sqrtf(sample * sample + knee * knee) - knee;
	return rect;
}


// These aren't valid distortion_effect_bysample functions, but they are preserved here for experimentation
float atanhf_clipping(float gain, float signal) {

	//Next three lines were suggested by Claude, because atanhf can give Nan/Inf results when the input is outside -1 - 1
	float driven = signal * gain;
    if (driven > 0.999f) driven = 0.999f;
    if (driven < -0.999f) driven = -0.999f;
	return atanhf(driven);
}

float cubic_soft_clipping(float gain, float signal) {
	
	float driven_signal = signal * gain;
	// we should be clamping the signal before the expression below. values outside { -1, 1 } blow up.

	return driven_signal - (1.0f / 3.0f) * powf(driven_signal, 3);
}

float simple_asym(float gain, float signal) {

	return powf(signal, gain) - 1;
	//what if I flipped this, and the signal was the exponent? What would that sound like?
}

//Wave shaping functions, processed over the entire buffer
void tanh_distortion_buffer(float* up_buf, int buf_size, int up_factor) {
	//distortion happens in the loop below
	for(int i = 0; i < (buf_size * up_factor); i++) {

		up_buf[i] = tanhf(up_buf[i]);
	}
}

// A space for stock distortion configs
void build_fav_distort_1(gain_engine* g_engine, int up_factor, int sample_rate, int buffer_size) {

	if (up_factor < 1) {
		up_factor = 1;										//force the up_factor to at least 1
	}
	int s_rate = sample_rate * up_factor;

	gain_init(g_engine, 2, up_factor, sample_rate, buffer_size);

    //initialize filters
    biquad* hp1 = bq_new(HIGHPASS, 160, 0.7071, 0, s_rate);
    biquad* hp2 = bq_new(HIGHPASS, 80, 0.7071, 0, s_rate);
	biquad* pk1 = bq_new(PEAK, 1500, 0.7071, 2.5119, s_rate);
    biquad* pk2 = bq_new(PEAK, 1000, 1, 2.5119, s_rate);
    biquad* lp1 = bq_new(LOWPASS, 3000, 0.7071, 0, s_rate);
    biquad* lp2 = bq_new(LOWPASS, 6000, 0.7071, 0, s_rate);

    //stack filters
    biquad* pre_filters1[3] = { hp1, lp1, pk1 };
	biquad* pre_filters2[3] = { hp2, lp2, pk2 };

	// initialize gain stages in place
    distortion_stage_init(&g_engine->slots[0].distortion, pre_filters1, 3, 1.0f, tanh_distortion, NULL, 0);
    biased_stage_init(&g_engine->slots[1].bias, pre_filters2, 3, 0.25f, 5.0f, tanh_distortion, NULL, 0);

}

// void build_treble_booster(gain_engine* g_engine, int up_factor, int sample_rate, int buffer_size) {

// 	if (up_factor < 1) {
// 		up_factor = 1;										//force the up_factor to at least 1
// 	}
// 	int s_rate = sample_rate * up_factor;

// 	gain_init(g_engine, 1, up_factor, s_rate, buffer_size);

// 	// clean_stage_init(g_engine->g_stages[0], NULL, 0, _0dB, NULL, 0);
// 	// g_engine->g_stages[0]->pre_filters[0] = bq_new(HIGHPASS, DFLT_TR_BS_CF, DFLT_TR_BS_Q, UNITY_GAIN, s_rate);
// 	static biquad* pre_filters[3];
// 	clean_stage_init(&g_engine->slots[0].clean, )

	
// }

// void build_octave_fuzz(gain_engine* g_engine, int sample_rate, int buffer_size) {

// 	int upsample_factor = 4;										// Oversample at a rate of 4 x original sample rate
// 	float rectification = 1;										// Apply full Wave rectification
// 	float bias = .10;												// apply a small positive bias to the signal
// 	float gain = plus_6dB;
// 	float dstr_wet = 0.5;
// 	int s_rate = sample_rate * upsample_factor;

// 	// initialize filters
// 	static biquad* high_pass;
// 	high_pass = bq_new(HIGHPASS, 20, BUTTERWORTH_Q, 1, s_rate);		// 2nd order Highpass at 20Hz, at unity gain
// 	static biquad* low_pass;
// 	low_pass = bq_new(LOWPASS, 3000, BUTTERWORTH_Q, 1, s_rate);		// 2nd order Lowpass at 3000Hz, at unity gain

// 	//stack filters
// 	static biquad* pre_filters[1];
// 	pre_filters[0] = high_pass;
// 	static biquad* post_filters[1];
// 	post_filters[0] = low_pass;

// 	// initialize distortion stages
// 	static gain_stage g_stage1;
// 	gain_stage_init(&g_stage1, pre_filters, 1, rectification, bias, gain, dstr_wet, tanh_distortion, post_filters, 1);

// 	//stack distortion stages
// 	static gain_stage* g_stages[1];
// 	g_stages[0] = &g_stage1;

// 	// set the control stage
// 	g_engine->control_stages[0] = 0;
// 	g_engine->control_stages[1] = 0;
// 	g_engine->control_stages[2] = 0;

// 	gain_init(g_engine, g_stages, 1, upsample_factor, sample_rate, buffer_size);

// 	// Rectify(fabsf())
// 	// High pass(20Hz)
// 	// Tanhf() with small bias
// 	// Another lower pass
// 	// Double signal, and expose dry/wet control 			try adding these to control defs from this function(should work)
// 	// Apply anti-aliasing

// }

// void build_octave_fuzz_v2(gain_engine* g_engine, int sample_rate, int buffer_size) {

// 	int upsample_factor = 4;										// Oversample at a rate of 4 x original sample rate
// 	float dflt_bias = 0.1f;											// apply a small positive bias to the signal
// 	float dflt_gain_1 = plus_3dB;									// default gain for the adjustable gain
// 	float fixed_gain_2 = plus_3dB;									// fixed gain for stage 2
// 	float fixed_gain_4 = plus_6dB;									// fixed gain for stage 4
// 	float dstr_wet = 0.65;											// default setting for dry/wet blend (65% wet)
// 	int s_rate = sample_rate * upsample_factor;						// adjusted sample rate for filter set up
	
// 	// stage 1
// 	static biquad* input_hpf;
// 	input_hpf = bq_new(HIGHPASS, 80, BUTTERWORTH_Q, UNITY_GAIN, s_rate);
// 	static biquad* stg1_fltrs[1];
// 	stg1_fltrs[0] = input_hpf;

// 	static gain_stage g_stage1;
// 	gain_stage_init(&g_stage1, stg1_fltrs, 1, 0, dflt_bias, dflt_gain_1, -1, tanh_distortion, NULL, 0);
// 	g_engine->control_stages[0] = 0;			// set the gain control to point at stage 1
// 	g_engine->control_stages[1] = 0;			// set the bias control to point at stage 2

// 	// stage 2
// 	static biquad* stg2_lpf;
// 	stg2_lpf = bq_new(LOWPASS, 5000, BUTTERWORTH_Q, UNITY_GAIN, s_rate);
// 	static biquad* stg2_hpf;
// 	stg2_hpf = bq_new(HIGHPASS, 150, BUTTERWORTH_Q, UNITY_GAIN, s_rate);

// 	static biquad* stg2_fltrs[2];
// 	stg2_fltrs[0] = stg2_lpf;
// 	stg2_fltrs[1] = stg2_hpf;

// 	static gain_stage g_stage2;
// 	gain_stage_init(&g_stage2, stg2_fltrs, 2, 0, 0, fixed_gain_2, -1, tanh_distortion, NULL, 0);

// 	// stage 3
// 	static biquad* dc_fltr;
// 	dc_fltr = bq_new(HIGHPASS, 20, BUTTERWORTH_Q, UNITY_GAIN, s_rate);

// 	static gain_stage g_stage3;
// 	gain_stage_init(&g_stage3, NULL, 0, 1, 0, _0dB, dstr_wet, distortion_passthrough, &dc_fltr, 1);
// 	g_engine->control_stages[2] = 2;			// set the dry/wet control to point at stage 3

// 	// stage 4
// 	static biquad* stg4_lpf;
// 	stg4_lpf = bq_new(LOWPASS, 3500, BUTTERWORTH_Q, UNITY_GAIN, s_rate);

// 	static gain_stage g_stage4;
// 	gain_stage_init(&g_stage4, NULL, 0, 0, 0, fixed_gain_4, -1, tanh_distortion, &stg4_lpf, 1);

// 	//stack distortion stages
// 	static gain_stage* g_stages[4];
// 	g_stages[0] = &g_stage1;
// 	g_stages[1] = &g_stage2;
// 	g_stages[2] = &g_stage3;
// 	g_stages[3] = &g_stage4;

// 	gain_init(g_engine, g_stages, 4, upsample_factor, sample_rate, buffer_size);

// 	// 4 stages
// 	// stage 1
// 	// gain control, highpass 80Hz, small bias, tanh
// 	// stage 2
// 	// highpass 150Hz, lowpass 5000Hz, fixed gain 1.5, tanh					Maybe a band pass instead of high and low
// 	// stage 3
// 	// soft rectify, highpass 20Hz, dry/wet blend
// 	// stage 4
// 	// fixed gain 2, tanh, lowpass 3500Hz

// }

// A blank function to drop individual g_stages into
void stage_testing(gain_engine* g_engine, int sample_rate, int buffer_size) {

	int upsample_factor = 1;

	gain_init(g_engine, 1, 1, sample_rate, buffer_size);
	general_stage_init(&g_engine->slots[0].general, NULL, 0, 0, 0, plus_30dB, -1, tanh_distortion, NULL, 0);

	g_engine->bypass = ON;
}