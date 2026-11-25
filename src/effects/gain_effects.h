#ifndef GAIN_FX_H
#define GAIN_FX_H

#include "Biquad.h"
#include "effects_engine.h"
#include "control_def.h"

#define		MAX_STAGE_PREFILTERS	3
#define		MAX_GAIN_STAGES			4
#define		MAX_STAGE_POSTFILTERS	3

#define 	MIN_GAIN_EXP_DIST		0.0316			// -30dB
#define 	MAX_GAIN_EXP_DIST		31.6228			// 30dB

// Treble Booster values
#define		DFLT_TR_BS_CF			1600			// This is where the resonant peak will sit
#define		MIN_TR_BS_Q				0.3
#define		MAX_TR_BS_Q				9
#define		DFLT_TR_BS_Q			3
#define		DFLT_TR_BS_Q_SET		0.31

#define		MIN_TR_BS_GAIN			_0dB
#define		MAX_TR_BS_GAIN			plus_30dB
#define		DFLT_TR_BS_GAIN			_0dB
#define		DFLT_TR_BS_GAIN_SET		0		

typedef void (*distortion_effect)(float*, int, int);
typedef float (*distortion_effect_bysample)(float);


//The controls this module offers to the UI. ui_listener registers this table, and the UI builds itself from it
//The order of the enum is the order of the table in distortion_effects.c
//To add a control: add a name here, and a row to the table
enum {
	DIST_CTRL_GAIN,
	DIST_CTRL_BIAS,
	DIST_CTRL_WET,
	DIST_CTRL_BYPS,
	// DIST_CTRL_LVL,
	DIST_NUM_CONTROLS
};
extern const ControlDef distortion_controls[DIST_NUM_CONTROLS];

typedef enum {
	CLEAN_STAGE			= 1,
	DISTORTION_STAGE	= 2,
	BIASED_STAGE		= 3,
	GENERAL_STAGE		= 9
} STAGE_TYPES;

// Later this should probably be added to the enum, but for now
// the definition below shows which index of control_stages holds 
// the index of which distortion_stage the control should point to.

// gain = control_stages[0];
// bias = control_stages[1];
// dry/wet = control_stages[2];

typedef struct gain_stage  gain_stage;			// forward declarations
typedef struct gain_engine gain_engine;			// full declaration below
typedef float (*gain_func)(gain_stage*, gain_engine*, float);
typedef void (*free_stage_func)(gain_stage*);

// Base type for gain stages
struct gain_stage{

	int type;
	gain_func process;			// The process function matched to the stage type
	free_stage_func free;

};

// A general use gain stage
typedef struct {

	gain_stage base;

	biquad* pre_filters[MAX_STAGE_PREFILTERS];		// A pointer to the array of pointers to the pre-distortion filters
	int num_pre_filters;							// length of the array of pointers
	float rectification;							// A float signal that tells the processor if and how to rectify the signal
	float bias;										// The bias factor, when asymetric distortion is desired
	float gain;										// The gain factor
	float dstr_wet;									// 0 = all dry, 1 = all wet
	distortion_effect_bysample d_effect;			// The waveshaping function(a.k.a, the actual distortion)
	biquad* post_filters[MAX_STAGE_POSTFILTERS];	// A pointer to the array of pointers to the post distortion filters
	int num_post_filters;							// length of the array of pointers

} general_gain_stage;

// A clean gain stage, that applies no wave shapping
typedef struct {

	gain_stage base;

	biquad* pre_filters[MAX_STAGE_PREFILTERS];		// A pointer to the array of pointers to the pre-distortion filters
	int num_pre_filters;							// length of the array of pointers
	float gain;										// The gain factor
	biquad* post_filters[MAX_STAGE_POSTFILTERS];		// A pointer to the array of pointers to the post distortion filters
	int num_post_filters;

} clean_gain_stage;

typedef struct {

	gain_stage base;

	biquad* pre_filters[MAX_STAGE_PREFILTERS];		// A pointer to the array of pointers to the pre-distortion filters
	int num_pre_filters;							// length of the array of pointers

	float gain;										// The gain factor
	distortion_effect_bysample d_effect;			// The waveshaping function(a.k.a, the actual distortion)

	biquad* post_filters[MAX_STAGE_POSTFILTERS];	// A pointer to the array of pointers to the post distortion filters
	int num_post_filters;							// length of the array of pointers

} distortion_gain_stage;

typedef struct {

	gain_stage base;

	biquad* pre_filters[MAX_STAGE_PREFILTERS];		// A pointer to the array of pointers to the pre-distortion filters
	int num_pre_filters;							// length of the array of pointers
	
	float bias;										// The bias factor, when asymetric distortion is desired
	float gain;										// The gain factor
	distortion_effect_bysample d_effect;			// The waveshaping function(a.k.a, the actual distortion)
	
	biquad* post_filters[MAX_STAGE_POSTFILTERS];	// A pointer to the array of pointers to the post distortion filters
	int num_post_filters;							// length of the array of pointers

} biased_gain_stage;

typedef union {

	gain_stage base;
	clean_gain_stage clean;
	distortion_gain_stage distortion;
	biased_gain_stage bias;
	general_gain_stage general;

} gain_stage_slot;

struct gain_engine{

	effects_engine base;					// included to imitate a supertype
	biquad* image_filter;					// A lowpass filter, calibrated for antt-imaging
	biquad* alias_filter;					// A lowpass filter, calibrated for anti-aliasing -- filters have a state to themselves, so we can't use one filter for both steps
	gain_stage_slot slots[MAX_GAIN_STAGES];	// holds the stage structs themselves
	gain_stage* g_stages[MAX_GAIN_STAGES];	// A pointer to the array of pointers to the gain stages
	
	int num_stages;							// The number of stages in the array
	//int control_stages[3];				// Holds the indices of the stage a control points to
	int up_factor;							// A multiple for upsampling. an value less than 2 will skip upsampling(and alias filtering) altogether
	float* oversampling_buffer;				// A pointer to a larger float buffer. It's size is buffer_size * up_factor
	int bypass;

	int buffer_size;						// The size of the buffer used by the audio stream(not for oversampling)

};



void gain_init(gain_engine* d_engine, /*gain_stage** d_stages,*/ int num_stages, int up_factor, int sample_rate, int buffer_size);
void general_stage_init(general_gain_stage* d_stage, biquad** pre_filters, int num_pre_filters, float rectification, float bias, float gain, float dstr_wet, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters);
void clean_stage_init(clean_gain_stage* g_stage, biquad** pre_filters, int num_pre_filters, float gain, biquad** post_filters, int num_post_filters);
void distortion_stage_init(distortion_gain_stage* g_stage, biquad** pre_filters, int num_pre_filters, float gain, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters);
void biased_stage_init(biased_gain_stage* g_stage, biquad** pre_filters, int num_pre_filters, float bias, float gain, distortion_effect_bysample d_effect, biquad** post_filters, int num_post_filters);

void process_gain(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size);
float process_general_stage(gain_stage* d_stage, gain_engine* g_engine, float sample);
float process_clean_stage(gain_stage* g_stage, gain_engine* g_engine, float sample);
float process_distortion_stage(gain_stage* stage, gain_engine* g_engine, float sample);
float process_biased_stage(gain_stage* stage, gain_engine* g_engine, float sample);

void gain_stage_free(gain_stage* d_stage);
void general_stage_free(gain_stage* stage);
void clean_stage_free(gain_stage* stage);
void distortion_stage_free(gain_stage* stage);
void biased_stage_free(gain_stage* stage);
void gain_free(gain_engine* d_engine);

float set_distortion_gain (effects_engine* engine, float gain);
float set_distortion_bias (effects_engine* engine, float bias);
float set_distortion_mix (effects_engine* engine, float wet);
float set_distortion_bypass(effects_engine* engine, float bypass);

//Wave shaping functions, processed sample-by-sample
float tanh_distortion(float sample);
float soft_knee_distortion(float sample);
float bitcrush_distortion(float sample);
float wavefolder_1(float sample);
float wavefolder_2(float sample);

// rectifiers
float rectify(float sample);
float soft_rectify(float sample, float knee);

float atanhf_clipping(float gain, float signal);
float cubic_soft_clipping(float gain, float signal);
float simple_asym(float gain, float signal);
// float abs_fuzz(float gain, float signal);

//Wave shaping functions, processed over the entire buffer
void tanh_distortion_buffer(float* up_buf, int buf_size, int up_factor);

//designed distortion effects
void build_fav_distort_1(gain_engine* d_engine, int up_factor, int sample_rate, int buffer_size);
void build_octave_fuzz(gain_engine* d_engine, int sample_rate, int buffer_size);
void build_octave_fuzz_v2(gain_engine* d_engine, int sample_rate, int buffer_size);
void stage_testing(gain_engine* d_engine, int sample_rate, int buffer_size);

#endif