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

//I can't get input and output buffers before the stream starts, so I can't get pointers for initialization
//These will have to be parameters given in process

void process_distortion(distortion_engine* d_engine, float *in_buf, float *out_buf) {

	float sample;

	//process distortion without oversampling
	for(int i = 0, j = 0; i < d_engine->buffer_size; i++) {
		
		sample = in_buf[i];
		for (int n = 0; n < d_engine->num_stages; n++) {
			//call the nth distortion_stage to process the ith sample
			sample = process_distort_stage(d_engine->d_stages[n], sample);
		}

		//output stereo
		out_buf[j++] = sample;
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

//Below are a variety of different functions for distortion, processed sample-by-sample
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

/*Below are a variety of different functions for distortion, processed over the entire buffer
* All of these functions include an anti-aliasing filter(brute force, 1st order low-pass)
* They take in a gain factor, the upsampled buffer, the native buffer size, and the upsampling factor
*/

void tanh_distortion_buffer(float* up_buf, int buf_size, int up_factor) {
	//distortion happens in the loop below
	for(int i = 0; i < (buf_size * up_factor); i++) {

		up_buf[i] = tanhf(up_buf[i]);
	}
}

//archived code

//This is an old version of engine and process, written before the shift to distortion_stages

// void distortion_init(distortion_engine* d_engine, distortion_effect distort_func, float gain, int up_factor, int sample_rate, int buffer_size, float* up_buf) {

// 	d_engine->alias_filter = alias_init(up_factor, sample_rate);
// 	d_engine->leading_HPF = leading_HPF_init(up_factor, sample_rate);
// 	d_engine->trailing_LPF = trailing_LPF_initint(up_factor, sample_rate);
// 	d_engine->d_effect = distort_func;
// 	d_engine->gain = gain;

// 	d_engine->up_factor = up_factor;
// 	d_engine->buffer_size = buffer_size;

// 	d_engine->up_buf = up_buf;

// }


// void process_distortion(distortion_engine* d_engine, float *in_buf, float *out_buf) {

// 	float bias = 0.25f;

// 	upsample(in_buf, d_engine->buffer_size, d_engine->up_buf, d_engine->up_factor);

// 	//leading_HPF_process(d_engine->leading_HPF, d_engine->buffer_size, d_engine->up_buf, d_engine->up_factor);

// 	apply_gain(d_engine->gain, d_engine->buffer_size, d_engine->up_buf, d_engine->up_factor);

// 	add_bias(bias, d_engine->buffer_size, d_engine->up_buf, d_engine->up_factor);

// 	d_engine->d_effect(d_engine->up_buf, d_engine->buffer_size, d_engine->up_factor);

// 	float bias_offset = compute_bias_offset(tanh_distortion, bias);

// 	filter_bias(bias_offset, d_engine->buffer_size, d_engine->up_buf, d_engine->up_factor);
	
// 	alias_filter_process_gc(d_engine->alias_filter, d_engine->buffer_size, d_engine->up_buf, d_engine->up_factor);
	
// 	downsample_stereo(d_engine->up_buf, d_engine->up_factor, out_buf, d_engine->buffer_size);

// 	//trailing_LPF_process(d_engine->trailing_LPF, d_engine->buffer_size, d_engine->up_buf, d_engine->up_factor);
// }