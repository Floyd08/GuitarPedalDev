#define _USE_MATH_DEFINES
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "dsp_tools.h"
#include "Biquad.h"
#include "distortion_effects.h"

//initialization. Should handle all instantiation for the distortion engine, so everything is ready for runtime
void distortion_init(distortion_engine* d_engine, distortion_stage** d_stages, int num_stages, int up_factor, int sample_rate, int buffer_size, float* up_buf){

	d_engine->alias_filter = alias_init(up_factor, sample_rate);
	d_engine->d_stages = d_stages;
	d_engine->num_stages = num_stages;
	d_engine->up_factor = up_factor;
	d_engine->buffer_size = buffer_size;
	d_engine->up_buf = up_buf;
}

void distortion_stage_init(distortion_stage* d_stage, biquad** pre_filters, int num_pre_filters, float bias, float gain, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters) {

	d_stage->pre_filters = pre_filters;
	d_stage->num_pre_filters = num_pre_filters;
	d_stage->bias = bias;
	d_stage->gain = gain;
	d_stage->d_effect = d_effect;
	d_stage->post_filters = post_filters;
	d_stage->num_post_filters = num_post_filters;
}

//Parameter getting and setting
void set_distort_gain (distortion_engine* d_engine, float gain) {

	float converted_gain = expf(gain * 3.41);
	int stage_index = d_engine->control_stage;
	d_engine->d_stages[stage_index]->gain = converted_gain;
}

void set_distort_bias (distortion_engine* d_engine, float bias) {

	int stage_index = d_engine->control_stage;
	d_engine->d_stages[stage_index]->bias = bias;
}

void process_distortion(distortion_engine* d_engine, float *in_buf, float *out_buf) {

	float sample;
	//process distortion without oversampling
	for(int i = 0, j = 0; i < d_engine->buffer_size; i++) {
		
		sample = in_buf[i];
		for (int n = 0; n < d_engine->num_stages; n++) {
			//call the nth distortion_stage to process the ith sample
			sample = process_distort_stage(d_engine->d_stages[n], sample);
		}
		out_buf[j++] = sample;
	}
}

float process_distort_stage(distortion_stage* d_stage, float sample) {

	float output = sample;

	//apply bias
	output = output + d_stage->bias;

	//apply gain
	output = output * d_stage->gain;

	//loop through the pre distortion filters
	for (int i = 0; i < d_stage->num_pre_filters; i++) {
		output = bq_process(d_stage->pre_filters[i], output);
	}

	//apply the waveshaping function
	output = d_stage->d_effect(output);

	//loop through the post distortion filters
	for (int i = 0; i < d_stage->num_post_filters; i++) {
		output = bq_process(d_stage->post_filters[i], output);
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
float tanh_distortion(float sample){
	return tanhf(sample);
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
void build_fav_distort_1(distortion_engine* d_engine, int up_factor, int sample_rate, int buffer_size, float* up_buf) {

    // initialize distortion stages
	static distortion_stage d_stage1;
    static distortion_stage d_stage2;

    //initialize filters
    static biquad* hp1;
    hp1 = bq_new(HIGHPASS, 160, 0.7071, 0, sample_rate);
    static biquad* hp2;
    hp2 = bq_new(HIGHPASS, 80, 0.7071, 0, sample_rate);
	static biquad* pk1;
    pk1 = bq_new(PEAK, 1500, 0.7071, 2.5119, sample_rate);
    static biquad* pk2;
    pk2 = bq_new(PEAK, 1000, 1, 2.5119, sample_rate);
    static biquad* lp1;
    lp1 = bq_new(LOWPASS, 3000, 0.7071, 0, sample_rate);
    static biquad* lp2;
    lp2 = bq_new(LOWPASS, 6000, 0.7071, 0, sample_rate);

    //stack filters
    static biquad* pre_filters1[3];
    pre_filters1[0] = hp1;
    pre_filters1[1] = lp1;
    pre_filters1[2] = pk1;
    distortion_stage_init(&d_stage1, pre_filters1, 3, 0.0f, 1.0f, tanh_distortion, NULL, 0);

    static biquad* pre_filters2[3];
    pre_filters2[0] = hp2;
    pre_filters2[1] = lp2;
    pre_filters2[2] = pk2;
    distortion_stage_init(&d_stage2, pre_filters2, 3, 0.25f, 5.0f, tanh_distortion, NULL, 0);

    //stack distortion stages
    static distortion_stage* d_stages[2];
    d_stages[0] = &d_stage1;
    d_stages[1] = &d_stage2;

    //set the control stage
    d_engine->control_stage = 1;

    distortion_init(d_engine, d_stages, 2, up_factor, sample_rate, buffer_size, up_buf);
}