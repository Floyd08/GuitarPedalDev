#ifndef DISTORTION_FX_H
#define DISTORTION_FX_H

#include "Biquad.h"
#include "effects_engine.h"
#include "control_def.h"

//#define M_E						2.718281828		// e to 9 decimal places
#define MIN_GAIN_EXP_DIST		0.0316			// -30dB
#define MAX_GAIN_EXP_DIST		31.6228			// 30dB

//these must be initialized in main, before the Audio stream starts
typedef void (*distortion_effect)(float*, int, int);
typedef float (*distortion_effect_bysample)(float);

//The controls this module offers to the UI. ui_listener registers this table, and the UI builds itself from it
//The order of the enum is the order of the table in distortion_effects.c
//To add a control: add a name here, and a row to the table
enum {
	DIST_CTRL_GAIN,
	DIST_CTRL_BIAS,
	DIST_CTRL_DSTR_WET,
	// DIST_CTRL_LVL,
	DIST_NUM_CONTROLS
};
extern const ControlDef distortion_controls[DIST_NUM_CONTROLS];

// Later this should probably be added to the enum, but for now
// the definition below shows which index of control_stages holds 
// the index of which distortion_stage the control should point to.

// gain = control_stages[0];
// bias = control_stages[1];
// dry/wet = control_stages[2];

typedef struct {

	biquad** pre_filters;				// A pointer to the array of pointers to the pre-distortion filters
	int num_pre_filters;				// length of the array of pointers
	float rectification;				// A float signal that tells the processor if and how to rectify the signal
	float bias;							// The bias factor, when asymetric distortion is desired
	float gain;							// The gain factor
	float dstr_wet;						// 0 = all dry, 1 = all wet
	distortion_effect_bysample d_effect;// The waveshaping function(a.k.a, the actual distortion)
	biquad** post_filters;				// A pointer to the array of pointers to the post distortion filters
	int num_post_filters;				// length of the array of pointers

} distortion_stage;

typedef struct {

	effects_engine base;				// included to imitate a supertype
	biquad* image_filter;				// A lowpass filter, calibrated for antt-imaging
	biquad* alias_filter;				// A lowpass filter, calibrated for anti-aliasing -- filters have a state to themselves, so we can't use one filter for both steps
	distortion_stage** d_stages;		// A pointer to the array of pointers to the distortion stages
	
	int num_stages;						// The number of stages in the array
	int control_stages[3];				// Holds the indices of the stage a control points to
	int up_factor;						// A multiple for upsampling. an value less than 2 will skip upsampling(and alias filtering) altogether
	float* oversampling_buffer;			// A pointer to a larger float buffer. It's size is buffer_size * up_factor
	
	int buffer_size;					// The size of the buffer used by the audio stream(not for oversampling)

} distortion_engine;

void distortion_init(distortion_engine* d_engine, distortion_stage** d_stages, int num_stages, int up_factor, int sample_rate, int buffer_size);
void distortion_stage_init(distortion_stage* d_stage, biquad** pre_filters, int num_pre_filters, float rectification, float bias, float gain, float dstr_wet, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters);
void set_distort_gain (effects_engine* engine, float gain);
void set_distort_bias (effects_engine* engine, float bias);
void set_distort_wet_mix (effects_engine* engine, float wet);
void process_distortion(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size);
float process_distort_stage(distortion_stage* d_stage, float sample);
void distortion_stage_free(distortion_stage* d_stage);
void distortion_free(distortion_engine* d_engine);

//Wave shaping functions, processed sample-by-sample
float tanh_distortion(float sample);
float soft_knee_distortion(float sample);
float atanhf_clipping(float gain, float signal);
float cubic_soft_clipping(float gain, float signal);
float sin_fuzz(float gain, float signal);
float simple_asym(float gain, float signal);
float abs_fuzz(float gain, float signal);

//Wave shaping functions, processed over the entire buffer
void tanh_distortion_buffer(float* up_buf, int buf_size, int up_factor);

//designed distortion effects
void build_fav_distort_1(distortion_engine* d_engine, int up_factor, int sample_rate, int buffer_size);
void build_octave_fuzz(distortion_engine* d_engine, int sample_rate, int buffer_size);
void build_octave_fuzz_v2(distortion_engine* d_engine, int sample_rate, int buffer_size);
void stage_testing(distortion_engine* d_engine, int sample_rate, int buffer_size);

#endif