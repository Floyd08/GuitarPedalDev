#ifndef COMPRESSOR_FX_H
#define COMPRESSOR_FX_H

#include "effects_engine.h"
#include "control_def.h"

#define	MIN_COMP_ATCK				0.1		//defined range for the attack control in milliseconds
#define	MAX_COMP_ATCK				100.0
#define DFLT_COMP_ATCK				10.0
#define DFLT_COMP_ATCK_SET			0.3

#define	MIN_COMP_RLSE				5.0		//defined range for the release control in milliseconds
#define	MAX_COMP_RLSE				2000.0
#define DFLT_COMP_RLSE				200.0
#define DFLT_COMP_RLSE_SET			0.1

// #define MIN_COMP_THRSH				minus_40dB		
// #define MAX_COMP_THRSH				_0dB
// #define DFLT_COMP_THRSH				minus_20dB
#define MIN_COMP_THRSH				-40		
#define MAX_COMP_THRSH				0
#define DFLT_COMP_THRSH				-20
#define	DFLT_COMP_THRSH_SET			0.5

#define MIN_COMP_MKGAIN				_0dB
#define MAX_COMP_MKGAIN				plus_15dB
#define DFLT_COMP_MKGAIN			plus_3dB
#define	DFLT_COMP_MKGAIN_SET		0.2

#define MIN_COMP_RATIO				1
#define MAX_COMP_RATIO				20
#define DFLT_COMP_RATIO				4
#define DFLT_COMP_RATIO_SET			0.2

#define MIN_COMP_LVL				minus_9dB
#define	MAX_COMP_LVL				plus_9dB
#define	DFLT_COMP_LVL				_0dB
#define DFLT_COMP_LVL_SET			0.5

// #define DFLT_ENV_ATCK				10.0
// #define DFLT_ENV_RLSE				50.0

// enumerates the controls that the UI should generate for this fx module
enum {
	COMP_CTRL_ATCK,
	COMP_CTRL_RLSE,
	COMP_CTRL_THRSH,
	COMP_CTRL_MKGAIN,
	COMP_CTRL_RATIO,
	COMP_CTRL_LVL,
	COMP_CTRL_BYPASS,
	COMP_NUM_CONTROLS
};
extern const ControlDef compressor_controls[COMP_NUM_CONTROLS];

typedef struct {

	effects_engine base;						// included to imitate a supertype
	// envelope_follower e_fol;					// An envelope follower object
	float attack_ms;
	float attack_alpha;
	float release_ms;
	float release_alpha;

	// float threshold;							// The linear threshold over which compression is triggered. Must be > 0
	float threshold_dB;							// The linear threshold over which compression is triggered. Must be > 0
	float makeup_gain;							// The maximum input gain to be applied, which gets scaled by the state of the envelope during compression
	float ratio;								// Used to calculate the esponent, needed in gain calculation must be >= 1
	float exponent;								// Pre-computated to avoid division in real-time

	float peak_dB;
	float reduction_dB;
	float level;

	int sample_rate;
	int bypass;

} compressor_engine;

void compressor_init(compressor_engine* c_engine, int sample_rate);
void process_compressor(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size);

float set_comp_attack(effects_engine* engine, float attack);
float set_comp_release(effects_engine* engine, float release);
float set_comp_mkp_gain(effects_engine* engine, float gain);
float set_comp_threshold(effects_engine* engine, float threshold);
float set_comp_ratio(effects_engine* engine, float ratio);
float set_comp_level(effects_engine* engine, float ratio);
float set_comp_bypass(effects_engine* engine, float bypass);

#endif