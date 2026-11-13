#ifndef BITCRSH_ENGINE_H
#define BITCRSH_ENGINE_H

#include <math.h>
#include "effects_engine.h"
#include "dsp_tools.h"
#include "control_def.h"

#define		MAX_BIT_DEPTH		12
#define		MAX_BIT_DEPTH_FL	0.334
#define		MIN_BIT_DEPTH		1

#define		DFLT_RATE_RTIO		0.05

#define		MAX_SAMPLE_RATE		0.4
#define		MIN_SAMPLE_RATE		0.05
#define		DFLT_SAMPLE_RATE	0.4
#define		DFLT_SAMPLE_RATE_SET 1 

#define		MIN_BIT_LP			3000
#define		MAX_BIT_LP			20000
#define		MIN_BIT_HP			20
#define		MAX_BIT_HP			800
#define		BIT_BP_Q			1.2
#define		DFLT_BP_SET			0.01

#define		MAX_BP_MK_GAIN		plus_9dB

// enumerate bticrusher controls
enum {
	BITCRSH_CTRL_BTDPTH,
	BITCRSH_CTRL_GAIN,
	BITCRSH_CTRL_RATE,
	BITCRSH_CTRL_BP,
	BITCRSH_NUM_CONTROLS
};
extern const ControlDef bitcrusher_controls[BITCRSH_NUM_CONTROLS];

typedef struct {

	effects_engine base;		// included to imitate a supertype
	float bit_depth;			// current bit depth
	float levels;				// computed from the bit depther setting (2^bit_depth)
	float scale;				// computed from levels (levels / 2)
	float gain;					// applied at input, to bring the level up and mitigate sputter

	float rate_ratio;			// used to compute the samples to hold for the "Sample-and-hold" procedure
	float held_sample;			// holds the current sample being output, for sample rate crushing
	float phase;				// accumulates rate_ratio. at sample_count > 1, grab a new sample
	float sample_rate;			// the native sample rate, used by the callback function
	
	biquad* lpf;				// A set of filters, high pass and low pass.
	biquad* hpf1;				// together they sweep from wide open
	biquad* hpf2;				// down to a "small-speaker" sound
	float bp_make_gain;			// Make up gain is applied as the band pass filter narrows

} bit_engine;

void bitcrush_init(bit_engine* b_engine, int sample_rate);
void process_bitcrush(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);

float set_btcrsh_depth(effects_engine* engine, float bit_depth);
float set_btcrsh_gain (effects_engine* engine, float gain);
float set_btcrsh_sample_rate(effects_engine* engine, float sample_rate);
float set_btcrsh_bp(effects_engine* engine, float band);

#endif