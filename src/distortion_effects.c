#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "dsp_tools.h"
#include "Biquad.h"
#include "control_def.h"
#include "effects_engine.h"
#include "distortion_effects.h"

//The controls this module offers to the UI. ui_listener registers this table, and the UI builds itself from it
//Rows are relative to this module (0 is its first row). Names must be unique across ALL modules
const ControlDef distortion_controls[DIST_NUM_CONTROLS] = {
	[DIST_CTRL_GAIN] = { .name = "gain", .label = "Gain", .type = CTRL_KNOB, .row = 0, .default_value = 0.10f, .update_func = set_distort_gain },
	[DIST_CTRL_BIAS] = { .name = "bias", .label = "Bias", .type = CTRL_KNOB, .row = 0, .default_value = 0.625f, .update_func = set_distort_bias },
	[DIST_CTRL_DSTR_WET] = { .name = "dstr_wet", .label = "Dry/Wet", .type = CTRL_KNOB, .row = 0, .default_value = 0.5f, .update_func = set_distort_wet_mix },
};

//initialization. Should handle all instantiation for the distortion engine, so everything is ready for runtime
void distortion_init(distortion_engine* d_engine, distortion_stage** d_stages, int num_stages, int up_factor, int sample_rate, int buffer_size) {

	effects_engine base;
	effects_engine_init(&base, "distortion");
	base.controls = distortion_controls;
	base.num_controls = DIST_NUM_CONTROLS;

	d_engine->base = base;
	d_engine->image_filter = alias_init(up_factor, sample_rate);
	d_engine->alias_filter = alias_init(up_factor, sample_rate);
	d_engine->d_stages = d_stages;
	d_engine->num_stages = num_stages;
	d_engine->up_factor = up_factor;
	d_engine->buffer_size = buffer_size;
	d_engine->oversampling_buffer = calloc( buffer_size * up_factor, sizeof(float) );
}

void distortion_stage_init(distortion_stage* d_stage, biquad** pre_filters, int num_pre_filters, float rectification, float bias, float gain, float dstr_wet, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters) {

	d_stage->pre_filters = pre_filters;
	d_stage->num_pre_filters = num_pre_filters;
	d_stage->rectification = rectification;
	d_stage->bias = bias;
	d_stage->gain = gain;
	d_stage->dstr_wet = dstr_wet;
	d_stage->d_effect = d_effect;
	d_stage->post_filters = post_filters;
	d_stage->num_post_filters = num_post_filters;
}

//Parameter getting and setting
void set_distort_gain (effects_engine* engine, float gain) {

	distortion_engine* d_engine = (distortion_engine*)engine;

	//float converted_gain = expf(gain * 3.41);
	float converted_gain = linear_to_log(gain, minus_3dB, plus_30dB);
	int stage_index = d_engine->control_stages[0];
	d_engine->d_stages[stage_index]->gain = converted_gain;
}

void set_distort_bias (effects_engine* engine, float bias) {

	distortion_engine* d_engine = (distortion_engine*)engine;

	// value * (new_max - new_min) + new_min		->		max 1 and min -1, gets us to 2 -1
	float new_bias = (bias * 2) - 1;				
	int stage_index = d_engine->control_stages[1];
	d_engine->d_stages[stage_index]->bias = new_bias;
}

void set_distort_wet_mix (effects_engine* engine, float wet) {

	distortion_engine* d_engine = (distortion_engine*)engine;
	
	int stage_index = d_engine->control_stages[2];
	d_engine->d_stages[stage_index]->dstr_wet = wet;
}

void process_distortion(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size) {

	distortion_engine* d_engine = (distortion_engine*)engine;

	float sample;

	if (d_engine->up_factor < 2) {

		for(int i = 0, j = 0; i < d_engine->buffer_size; i++) {
			
			sample = in_buf[i];
			for (int n = 0; n < d_engine->num_stages; n++) {
				//call the nth distortion_stage to process the ith sample
				sample = process_distort_stage(d_engine->d_stages[n], sample);
			}
			out_buf[j++] = sample;
		}
	}
	else {
		
		upsample(in_buf, buffer_size, d_engine->oversampling_buffer, d_engine->up_factor);				// zero stuffing increases the sample rate
		
		int oversampling_buffer_length = buffer_size * d_engine->up_factor;
		for(int i = 0, j = 0; i < oversampling_buffer_length; i++) {
			
			sample = d_engine->oversampling_buffer[i];
			sample = bq_process(d_engine->image_filter, sample);							// anti-imaging filter smooths the signal
			sample  = sample * d_engine->up_factor;											// compensate for gain loss
			
			for (int n = 0; n < d_engine->num_stages; n++) {
				//call the nth distortion_stage to process the ith sample
				//printf("processing stage: %d, on sample: %f\n", n, sample);
				if (d_engine->d_stages[n] == NULL) {
					fprintf(stderr, "distortion stage %d is NULL\n", n);
				}
				sample = process_distort_stage(d_engine->d_stages[n], sample);
			}
			sample = bq_process(d_engine->alias_filter, sample);							// anti-aliasing filter
			d_engine->oversampling_buffer[j++] = sample;
		}

		downsample_mono(d_engine->oversampling_buffer, d_engine->up_factor, out_buf, buffer_size);		// restore to the original sample rate
	}
}

float process_distort_stage(distortion_stage* d_stage, float sample) {

	float output = sample;
	
	//apply gain
	output = output * d_stage->gain;

	//rectify, if desired
	if (d_stage->rectification == 1) {
		output = soft_rectify(output, 0.8);
	}
	
	//loop through the pre distortion filters
	for (int i = 0; i < d_stage->num_pre_filters; i++) {
		output = bq_process(d_stage->pre_filters[i], output);
	}
	
	//apply bias
	output = output + d_stage->bias;
	
	//apply the waveshaping function and remove the bias
	output = d_stage->d_effect(output) - d_stage->d_effect(d_stage->bias);
	
	//loop through the post distortion filters
	for (int i = 0; i < d_stage->num_post_filters; i++) {
		output = bq_process(d_stage->post_filters[i], output);
	}
	
	if (d_stage->dstr_wet != -1) {
		output = output * (d_stage->dstr_wet) + output * (1.0f - d_stage->dstr_wet);
	}
	
	return output;
}

void distortion_free(distortion_engine* d_engine) {

	bq_destroy(d_engine->alias_filter);

	for (int i = 0; i < d_engine->num_stages; i++) {
		distortion_stage_free(d_engine->d_stages[i]);
	}

	free(d_engine);
}

void distortion_stage_free(distortion_stage* d_stage) {

	for (int i = 0; i < d_stage->num_pre_filters; i++) {
		bq_destroy(d_stage->pre_filters[i]);
	}

	for (int i = 0; i < d_stage->num_post_filters; i++) {
		bq_destroy(d_stage->post_filters[i]);
	}

	free(d_stage);
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

float atanhf_clipping(float gain, float signal) {

	//Next three lines were suggested by Claude, because atanhf can give Nan/Inf results when the input is outside -1 - 1
	float driven = signal * gain;
    if (driven > 0.999f) driven = 0.999f;
    if (driven < -0.999f) driven = -0.999f;
	return atanhf(signal * gain);
}

float cubic_soft_clipping(float gain, float signal) {
	
	float driven_signal = signal * gain;

	return driven_signal - (1.0f / 3.0f) * powf(driven_signal, 3);
}

float sin_fuzz(float gain, float signal) {

	return sinf(gain * signal);
}

float simple_asym(float gain, float signal) {

	return powf(signal, gain) - 1;
	//what if I flipped this, and the signal was the exponent? What would that sound like?
}

float abs_fuzz(float gain, float signal) {
	
	float driven_signal = signal *gain;
	return driven_signal / (1 + fabsf(driven_signal));
}

//Wave shaping functions, processed over the entire buffer
void tanh_distortion_buffer(float* up_buf, int buf_size, int up_factor) {
	//distortion happens in the loop below
	for(int i = 0; i < (buf_size * up_factor); i++) {

		up_buf[i] = tanhf(up_buf[i]);
	}
}

// A space for stock distortion configs
void build_fav_distort_1(distortion_engine* d_engine, int up_factor, int sample_rate, int buffer_size) {

	if (up_factor < 1) {
		up_factor = 1;										//force the up_factor to at least 1
	}
	int s_rate = sample_rate * up_factor;

    // initialize distortion stages
	static distortion_stage d_stage1;
    static distortion_stage d_stage2;

    //initialize filters
    static biquad* hp1;
    hp1 = bq_new(HIGHPASS, 160, 0.7071, 0, s_rate);
    static biquad* hp2;
    hp2 = bq_new(HIGHPASS, 80, 0.7071, 0, s_rate);
	static biquad* pk1;
    pk1 = bq_new(PEAK, 1500, 0.7071, 2.5119, s_rate);
    static biquad* pk2;
    pk2 = bq_new(PEAK, 1000, 1, 2.5119, s_rate);
    static biquad* lp1;
    lp1 = bq_new(LOWPASS, 3000, 0.7071, 0, s_rate);
    static biquad* lp2;
    lp2 = bq_new(LOWPASS, 6000, 0.7071, 0, s_rate);

    //stack filters
    static biquad* pre_filters1[3];
    pre_filters1[0] = hp1;
    pre_filters1[1] = lp1;
    pre_filters1[2] = pk1;
    distortion_stage_init(&d_stage1, pre_filters1, 3, 0.0f, 0.0f, 1.0f, 1.0f, tanh_distortion, NULL, 0);

    static biquad* pre_filters2[3];
    pre_filters2[0] = hp2;
    pre_filters2[1] = lp2;
    pre_filters2[2] = pk2;
    distortion_stage_init(&d_stage2, pre_filters2, 3, 0.0f, 0.25f, 5.0f, 1.0f, tanh_distortion, NULL, 0);

    //stack distortion stages
    static distortion_stage* d_stages[2];
    d_stages[0] = &d_stage1;
    d_stages[1] = &d_stage2;

    //set the control stage
    d_engine->control_stages[0] = 1;
	d_engine->control_stages[1] = 1;
	d_engine->control_stages[2] = 1;

    distortion_init(d_engine, d_stages, 2, up_factor, sample_rate, buffer_size);
}

void build_octave_fuzz(distortion_engine* d_engine, int sample_rate, int buffer_size) {

	int upsample_factor = 4;										// Oversample at a rate of 4 x original sample rate
	float rectification = 1;										// Apply full Wave rectification
	float bias = .10;												// apply a small positive bias to the signal
	float gain = plus_6dB;
	float dstr_wet = 0.5;
	int s_rate = sample_rate * upsample_factor;

	// initialize filters
	static biquad* high_pass;
	high_pass = bq_new(HIGHPASS, 20, BUTTERWORTH_Q, 1, s_rate);		// 2nd order Highpass at 20Hz, at unity gain
	static biquad* low_pass;
	low_pass = bq_new(LOWPASS, 3000, BUTTERWORTH_Q, 1, s_rate);		// 2nd order Lowpass at 3000Hz, at unity gain

	//stack filters
	static biquad* pre_filters[1];
	pre_filters[0] = high_pass;
	static biquad* post_filters[1];
	post_filters[0] = low_pass;

	// initialize distortion stages
	static distortion_stage d_stage1;
	distortion_stage_init(&d_stage1, pre_filters, 1, rectification, bias, gain, dstr_wet, tanh_distortion, post_filters, 1);

	//stack distortion stages
	static distortion_stage* d_stages[1];
	d_stages[0] = &d_stage1;

	// set the control stage
	d_engine->control_stages[0] = 0;
	d_engine->control_stages[1] = 0;
	d_engine->control_stages[2] = 0;

	distortion_init(d_engine, d_stages, 1, upsample_factor, sample_rate, buffer_size);

	// Rectify(fabsf())
	// High pass(20Hz)
	// Tanhf() with small bias
	// Another lower pass
	// Double signal, and expose dry/wet control 			try adding these to control defs from this function(should work)
	// Apply anti-aliasing

}

void build_octave_fuzz_v2(distortion_engine* d_engine, int sample_rate, int buffer_size) {

	int upsample_factor = 4;										// Oversample at a rate of 4 x original sample rate
	float dflt_bias = 0.1f;											// apply a small positive bias to the signal
	float dflt_gain_1 = plus_3dB;									// default gain for the adjustable gain
	float fixed_gain_2 = plus_3dB;									// fixed gain for stage 2
	float fixed_gain_4 = plus_6dB;									// fixed gain for stage 4
	float dstr_wet = 0.65;											// default setting for dry/wet blend (65% wet)
	int s_rate = sample_rate * upsample_factor;						// adjusted sample rate for filter set up
	
	// stage 1
	static biquad* input_hpf;
	input_hpf = bq_new(HIGHPASS, 80, BUTTERWORTH_Q, UNITY_GAIN, s_rate);
	static biquad* stg1_fltrs[1];
	stg1_fltrs[0] = input_hpf;

	static distortion_stage d_stage1;
	distortion_stage_init(&d_stage1, stg1_fltrs, 1, 0, dflt_bias, dflt_gain_1, -1, tanh_distortion, NULL, 0);
	d_engine->control_stages[0] = 0;			// set the gain control to point at stage 1
	d_engine->control_stages[1] = 0;			// set the bias control to point at stage 2

	// stage 2
	static biquad* stg2_lpf;
	stg2_lpf = bq_new(LOWPASS, 5000, BUTTERWORTH_Q, UNITY_GAIN, s_rate);
	static biquad* stg2_hpf;
	stg2_hpf = bq_new(HIGHPASS, 150, BUTTERWORTH_Q, UNITY_GAIN, s_rate);

	static biquad* stg2_fltrs[2];
	stg2_fltrs[0] = stg2_lpf;
	stg2_fltrs[1] = stg2_hpf;

	static distortion_stage d_stage2;
	distortion_stage_init(&d_stage2, stg2_fltrs, 2, 0, 0, fixed_gain_2, -1, tanh_distortion, NULL, 0);

	// stage 3
	static biquad* dc_fltr;
	dc_fltr = bq_new(HIGHPASS, 20, BUTTERWORTH_Q, UNITY_GAIN, s_rate);

	static distortion_stage d_stage3;
	distortion_stage_init(&d_stage3, NULL, 0, 1, 0, _0dB, dstr_wet, distortion_passthrough, &dc_fltr, 1);
	d_engine->control_stages[2] = 2;			// set the dry/wet control to point at stage 3

	// stage 4
	static biquad* stg4_lpf;
	stg4_lpf = bq_new(LOWPASS, 3500, BUTTERWORTH_Q, UNITY_GAIN, s_rate);

	static distortion_stage d_stage4;
	distortion_stage_init(&d_stage4, NULL, 0, 0, 0, fixed_gain_4, -1, tanh_distortion, &stg4_lpf, 1);

	//stack distortion stages
	static distortion_stage* d_stages[4];
	d_stages[0] = &d_stage1;
	d_stages[1] = &d_stage2;
	d_stages[2] = &d_stage3;
	d_stages[3] = &d_stage4;

	distortion_init(d_engine, d_stages, 4, upsample_factor, sample_rate, buffer_size);

	// 4 stages
	// stage 1
	// gain control, highpass 80Hz, small bias, tanh
	// stage 2
	// highpass 150Hz, lowpass 5000Hz, fixed gain 1.5, tanh					Maybe a band pass instead of high and low
	// stage 3
	// soft rectify, highpass 20Hz, dry/wet blend
	// stage 4
	// fixed gain 2, tanh, lowpass 3500Hz

}

// A blank function to drop individual d_stages into
// void stage_testing(distortion_engine* d_engine, int sample_rate, int buffer_size) {

// 	int upsample_factor = 4;										// Oversample at a rate of 4 x original sample rate
// 	// int s_rate = sample_rate * upsample_factor;						// adjusted sample rate for filter set up

// 	// static biquad* stg2_lpf;
// 	// stg2_lpf = bq_new(LOWPASS, 5000, BUTTERWORTH_Q, UNITY_GAIN, s_rate);
// 	// static biquad* stg2_hpf;
// 	// stg2_hpf = bq_new(HIGHPASS, 150, BUTTERWORTH_Q, UNITY_GAIN, s_rate);
// 	// static biquad* stg2_fltrs[2];
// 	// stg2_fltrs[0] = stg2_lpf;
// 	// stg2_fltrs[1] = stg2_hpf;

// 	// static biquad* post_lpf1;
// 	// post_lpf1 = bq_new(LOWPASS, 5000, BUTTERWORTH_Q, UNITY_GAIN, s_rate);
// 	// static biquad* post_lpf2;
// 	// post_lpf2 = bq_new(LOWPASS, 3500, BUTTERWORTH_Q, UNITY_GAIN, s_rate);
// 	// static biquad* post_fltrs[1];
// 	// //post_fltrs[0] = post_lpf1;
// 	// post_fltrs[0] = post_lpf2;

// 	static distortion_stage d_stage1;
// 	distortion_stage_init(&d_stage1, NULL, 0, 0, 0, _0dB, -1, soft_knee_distortion, NULL, 0);

// 	//stack distortion stages
// 	static distortion_stage* d_stages[1];
// 	d_stages[0] = &d_stage1;

// 	// set the control stage
// 	d_engine->control_stages[0] = 0;
// 	d_engine->control_stages[1] = 0;
// 	d_engine->control_stages[2] = 0;

// 	distortion_init(d_engine, d_stages, 1, upsample_factor, sample_rate, buffer_size);
// }