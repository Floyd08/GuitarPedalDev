#ifndef	REVERB_EFFECTS_H
#define	REVERB_EFFECTS_H

#include <stdio.h>
#include "effects_engine.h"
#include "dsp_tools.h"

// default delay line length, used in both allpass filters and comb filters
#define		DFLT_DL_SIZE					16384				// 2^14 samples, aprx 372 ms

// series all pass reverb parameters
#define		SERIES_AP_NUM_ALLPASS			5
#define		SERIES_AP_1_MS					100
#define		SERIES_AP_2_MS					68
#define		SERIES_AP_3_MS					60
#define		SERIES_AP_4_MS					19.7
#define		SERIES_AP_5_MS					5.85

// freeverb reverb parameters
#define		FREEVERB_NUM_ALLPASS			4
#define		FREEVERB_AP_MS					{12.6, 10, 7.73, 5.1}
#define		DFLT_AP_GAIN					0.75
#define		FREEVERB_NUM_COMBS				8
#define		FREEVERB_CB_MS					{25.36, 27, 29.02, 30.81, 32.31, 33.88, 35.38, 36.75}
#define		DFLT_COMB_GAIN					0.25
#define		COMB_LPF_DFLT_CUTOFF			10000
#define		COMB_LPF_MIN					1500
#define		COMB_LPF_MAX					15000
#define		DFLT_LPF_TONE					0.6
#define		MAX_RT60_S						4.0f
#define		MIN_RT60_S						0.2f

// general reverb parameters
#define		MAX_VERB_GAIN		0.98
#define		DFLT_VERB_GAIN		0.75
#define		MIN_VERB_GAIN		0.6
#define		DFLT_VERB_WET		0.25
#define		MAX_VERB_WET		3
#define		MAX_VERB_DRY		2

// enumerate reverb controls
// series_ap controls
enum {
	S5_RVRB_CTRL_VERB,
	S5_RVRB_CTRL_DECAY,
	S5_RVRB_NUM_CONTROLS
};
extern const ControlDef series_ap_reverb_controls[S5_RVRB_NUM_CONTROLS];
// freeverb controls
enum {
	FREEVERB_CTRL_VERB,
	FREEVERB_CTRL_DECAY,
	FREEVERB_CTRL_TONE,
	FREEVERB_NUM_CONTROLS
};
extern const ControlDef freeverb_reverb_controls[FREEVERB_NUM_CONTROLS];

typedef struct {

	effects_engine base;				// included to imitate a supertype

	allpass_filter ap_array[SERIES_AP_NUM_ALLPASS];	// an array of allpass filters
	float decay_gain;
	float wet_gain;
	float dry_gain;

} series_ap_reverb_engine;

typedef struct {

	effects_engine base;								// included to imitate a supertype
	
	allpass_filter ap_array[FREEVERB_NUM_ALLPASS];		// an array of allpass filters
	verb_comb_filter comb_array[FREEVERB_NUM_COMBS];	// an array of comb filters
	
	float wet_gain;										// Used to set the dry/wet mix of the effect
	float dry_gain;
	float sample_rate;									// cached, to recalculate the low pass when the cutoff changes

} freeverb_reverb_engine;

// One of Schroeder's reverb designs, built on 5 allpass filters in series
void series_ap_reverb_engine_init(series_ap_reverb_engine* r_engine, int sample_rate);
void process_series_ap_reverb(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);

void set_sap_rvrb_decay(effects_engine* engine, float decay);
void set_sap_rvrb_wet_mix(effects_engine* engine, float wet_mix);

// a Freeverb style reverb design
void freeverb_reverb_engine_init(freeverb_reverb_engine* r_engine, int sample_rate);
void process_freeverb(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);

void set_freeverb_wet_mix(effects_engine* engine, float wet_mix);
void set_freeverb_decay(effects_engine* engine, float decay);
void set_freeverb_lpf_freq(effects_engine* engine, float cutoff);

#endif