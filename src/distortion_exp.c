#include "distortion_effects.h"
#include "distortion_exp.h"

/*
*	This module is intended as a laboratory, to build and test distortion effects 
*	without concern for latency in the callback or extensibility of design
*/


void exp_distortion_init(exp_distortion_engine* d_engine, distortion_effect distort_func, float gain, int up_factor, int sample_rate, int buffer_size, float* up_buf) {

	//d_engine->alias_filter = alias_init(up_factor, sample_rate);
	d_engine->hp1 = bq_new(HIGHPASS, 160, 0.7071, 0, sample_rate);
	d_engine->hp2 = bq_new(HIGHPASS, 80, 0.7071, 0, sample_rate);
	d_engine->pk1 = bq_new(PEAK, 1500, 0.7071, 2.5119,sample_rate);
	d_engine->pk2 = bq_new(PEAK, 1000, 1, 2.5119, sample_rate);
	d_engine->lp1 = bq_new(LOWPASS, 3000, 0.7071, 0, sample_rate);
	d_engine->lp2 = bq_new(LOWPASS, 6000, 0.7071, 0, sample_rate); 

	d_engine->d_effect = distort_func;
	d_engine->gain = gain;

	d_engine->up_factor = up_factor;
	d_engine->buffer_size = buffer_size;

	d_engine->up_buf = up_buf;

}

void process_distortion_exp1(exp_distortion_engine* d_engine, float *in_buf, float *out_buf) {

	float sample, bias = 0.25;

	for(int i = 0, j = 0; i < d_engine->buffer_size; i++) {

		//stage 1
		sample = bq_process(d_engine->hp1, in_buf[i]);
		sample = bq_process(d_engine->lp1, sample);
		sample = bq_process(d_engine->pk1, sample);
		sample = tanh_distortion(sample);

		//stage 2
		sample = sample + bias;
		sample = sample * d_engine->gain;
		sample = bq_process(d_engine->hp2, sample);
		sample = bq_process(d_engine->lp2, sample);
		sample = bq_process(d_engine->pk2, sample);
		sample = tanh_distortion(sample);

		//output stereo
		out_buf[j++] = sample;
		out_buf[j++] = sample;
	}
}