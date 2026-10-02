#ifndef DISTORTION_FX_H
#define DISTORTION_FX_H

#include "Biquad.h"

//these must be initialized in main, before the Audio stream starts

typedef void (*distortion_effect)(float*, int, int);
typedef float (*distortion_effect_bysample)(float);

typedef struct {

	biquad** pre_filters;				// A pointer to the array of pointers to the pre-distortion filters
	int num_pre_filters;				// length of the array of pointers
	float bias;							// The bias factor, when asymetric distortion is desired
	float gain;							// The gain factor
	distortion_effect_bysample d_effect;// The waveshaping function(a.k.a, the actual distortion)
	biquad** post_filters;				// A pointer to the array of pointers to the post distortion filters
	int num_post_filters;				// length of the array of pointers

} distortion_stage;

typedef struct {

	biquad* alias_filter;				// A lowpass filter, calibrated for anti-aliasing
	distortion_stage** d_stages;		// A pointer to the array of pointers to the distortion stages
	int num_stages;						// The number of stages in the array

	int up_factor;						// A multiple for upsampling. an value less than 2 will skip upsampling(and alias filtering) altogether
	int buffer_size;					// The size of the buffer used by the audio stream

	float* up_buf;						// A pointer to a larger float buffer. It's size is buffer_size * up_factor

} distortion_engine;

void distortion_init(distortion_engine* d_engine, distortion_stage** d_stages, int num_stages, int up_factor, int sample_rate, int buffer_size, float* up_buf);
void distortion_stage_init(distortion_stage* d_stage, biquad** pre_filters, int num_pre_filters, float bias, float gain, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters);
void process_distortion(distortion_engine* d_engine, float *in_buf, float *out_buf);
float process_distort_stage(distortion_stage* d_stage, float sample);
void distortion_stage_free(distortion_stage* d_stage);
void distortion_free(distortion_engine* d_engine);

//Simple, per sample distortion functions
float tanh_distortion(float sample);
float atanhf_clipping(float gain, float signal);
float cubic_soft_clipping(float gain, float signal);
float sin_fuzz(float gain, float signal);
float simple_asym(float gain, float signal);
float abs_fuzz(float gain, float signal);

//Below are a variety of different functions for distortion, processed over the entire buffer
//All of these functions include an anti-aliasing filter(brute force, 1st order low-pass)
//They take in a gain factor, the input and output buffers as well as a larger buffer to handle the upsampling, the global sample rate, and the upsampling factor
void tanh_distortion_buffer(float* up_buf, int buf_size, int up_factor);



// typedef struct {
// 	distortion_effect d_effect;
// 	biquad* alias_filter;														//A lowpass filter, calibrated for anti-aliasing
// 	biquad* leading_HPF;														//A Highpass filter, to run between upsampling and distortion
// 	biquad* trailing_LPF;														//Another low pass filter, to run after decimation													

// 	//configurable parameters
// 	float gain;																		
	
// 	//size parameters
// 	int up_factor;
// 	int buffer_size;

// 	//buffer parameters
// 	float* up_buf;

// } distortion_engine;


#endif