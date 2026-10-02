#ifndef DISTORTION_EXP_H
#define DISTORTION_EXP_H

#include "distortion_effects.h"
#include "distortion_exp.h"

typedef struct {
	distortion_effect d_effect;
	//biquad* alias_filter;														//A lowpass filter, calibrated for anti-aliasing
	
	biquad* hp1;
	biquad* lp1;
	biquad* pk1;
	biquad* pk2;
	biquad* hp2;
	biquad* lp2;

	//configurable parameters
	float gain;																		
	
	//size parameters
	int up_factor;
	int buffer_size;

	//buffer parameters
	float* up_buf;

} exp_distortion_engine;

void exp_distortion_init(exp_distortion_engine* d_engine, distortion_effect distort_func, float gain, int up_factor, int sample_rate, int buffer_size, float* up_buf);
void process_distortion_exp1(exp_distortion_engine* d_engine, float *in_buf, float *out_buf);

#endif