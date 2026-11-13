#ifndef	MODULATION_FX_H
#define MODULATION_FX_H

#include "effects_engine.h"
#include "dsp_tools.h"

// chorus constants
#define		CH_BUFF_SIZE		2048	// max expected delay is 40ms, 2048 samples should be plenty of room at 48kHz
#define		CH_BASE_DLY			20.0f	// in ms 
#define		MIN_CH_RATE			0.1f	// in Hz
#define		MAX_CH_RATE			3.0f
#define		DFLT_CH_RATE		1.0f
#define		DFLT_CH_RATE_SET	0.3f
#define		MIN_CH_DEPTH		1.0f	// in ms
#define		MAX_CH_DEPTH		10.0f	// in ms
#define		DFLT_CH_DEPTH		5.0f	// in ms
#define		DFLT_CH_DEPTH_SET	0.5f	// in ms
#define		DFLT_CH_WET			0.3f
#define		DFLT_CH_FDBK		0.0f	// unused

// flanger constants
#define		FLNG_BUFF_SIZE		512		// max expected delay is 8ms, 512 samples should be plenty of room at 48kHz
#define		FLNG_BASE_DLY		1.5f	// in ms

#define		MIN_FLNG_RATE		0.05f	// in Hz
#define		MAX_FLNG_RATE		1.0f
#define		DFLT_FLNG_RATE		0.3f
#define		DFLT_FLNG_RATE_SET	0.3f

#define		MIN_FLNG_DEPTH		0.5f	// in ms
#define		MAX_FLNG_DEPTH		3.0f	// in ms
#define		DFLT_FLNG_DEPTH		1.0f	// in ms
#define		DFLT_FLNG_DEPTH_SET	0.2f	// in ms

#define		MIN_FLNG_FDBK		-0.9f
#define		MAX_FLNG_FDBK		0.9f
#define		DFLT_FLNG_FDBK		0.54f
#define		DFLT_FLNG_FDBK_SET	0.8f

#define		MIN_FLNG_FRWD		-1.0f
#define		MAX_FLNG_FRWD		1.0f
#define		DFLT_FLNG_FRWD		0.5f
#define		DFLT_FLNG_FRWD_SET	0.75f

#define		MIN_FLNG_DRY		0.0f
#define		MAX_FLNG_DRY		1.0f
#define		DFLT_FLNG_DRY		0.8f
#define		DFLT_FLNG_DRY_SET	0.8f

#define		MIN_FLNG_HPF		60.0f
#define		MAX_FLNG_HPF		200.0f
#define		DFLT_FLNG_HPF		120.0f
#define		DFLT_FLNG_HPF_SET	0.429f

// phaser constants
#define		PHS_NUM_ALLPASS		12					// number of 1st order Allpass filters in the phaser's array
#define		DFLT_PHS_CENTRE		800					// sets the centre frequency of the allpass filters

#define		MIN_PHS_RATE		0.5f	// in Hz
#define		MAX_PHS_RATE		1.0f
#define		DFLT_PHS_RATE		0.5f
#define		DFLT_PHS_RATE_SET	0.3f

#define		MIN_PHS_FLTRS		1
#define		MAX_PHS_FLTRS		PHS_NUM_ALLPASS
#define		DFLT_PHS_FLTRS		PHS_NUM_ALLPASS
#define		DFLT_PHS_SET		1

#define		MIN_PHS_DEPTH		0.5f	// in octaves
#define		MAX_PHS_DEPTH		3.0f	
#define		DFLT_PHS_DEPTH		2.0f	
#define		DFLT_PHS_DEPTH_SET	0.6f

#define		MIN_PHS_DRY			0.01f	// linear <= 1
#define		MAX_PHS_DRY			1.0f	
#define		DFLT_PHS_DRY		0.5f	
#define		DFLT_PHS_DRY_SET	0.5f

#define		MIN_PHS_WET			0.01f	// linear <= 1
#define		MAX_PHS_WET			1.0f	
#define		DFLT_PHS_WET		1.0f	
#define		DFLT_PHS_WET_SET	1.0f

#define		DFLT_NUM_FLTR_SET	1	

// enumerate modulation effect controls
// chorus controls
enum {
	CHRS_CTRL_RATE,
	CHRS_CTRL_DEPTH,
	CHRS_CTRL_WET,
	CHRS_CTRL_BYPASS,
	CHRS_NUM_CONTROLS
};
extern const ControlDef chorus_controls[CHRS_NUM_CONTROLS];

enum {
	FLNG_CTRL_RATE,
	FLNG_CTRL_DEPTH,
	FLNG_CTRL_FDBK,
	FLNG_CTRL_FRWD,
	FLNG_CTRL_BLND,
	FLNG_CTRL_HPF,
	FLNG_CTRL_BYPASS,
	FLNG_NUM_CONTROLS
};
extern const ControlDef flanger_controls[FLNG_NUM_CONTROLS];

enum {
	PHS_CTRL_RATE,
	PHS_CTRL_DEPTH,
	PHS_CTRL_DRY,
	PHS_CTRL_WET,
	PHS_CTRL_FILTERS,
	PHS_CTRL_BYPASS,
	PHS_NUM_CONTROLS
};
extern const ControlDef phaser_controls[PHS_NUM_CONTROLS];

typedef struct {

	effects_engine base;				// included to imitate a supertype
	low_freq_oscillator lfo;			// A low frequency oscillator, to modulate the delay time
	ring_buffer chorus_line;			// A delay line for the chorus effect

	float base_delay_ms;				// The base delay, modulated by the lfo
	float base_delay_samples;
	float depth_ms;						// scaled by the output from the LFO. Range: 1 to 10 ms, dflt
	float depth_samples;
	float wet_mix;						// Dry/Wet mix control
	int bypass;	

	float samples_per_ms;				// sample rate / 1000, pre-computed
	int sample_rate;

} chorus_modulation_engine;

typedef struct {

	effects_engine base;				// included to imitate a supertype
	low_freq_oscillator lfo;			// A low frequency oscillator, to modulate the delay time
	ring_buffer chorus_line;			// A delay line for the chorus effect
	biquad* hpf;						// filters the feedback into the delay line

	float base_delay_ms;				// The base delay, modulated by the lfo
	float base_delay_samples;
	float depth_ms;						// scaled by the output from the LFO.
	float depth_samples;

	float feedback_gain;				// gain applied to the feedback for flanger
	float feedforward_gain;				// wet level and polarity, set notch depth
	float blend;						// Dry level
	int bypass;	
	
	float samples_per_ms;				// sample rate / 1000, pre-computed
	int sample_rate;

} flanger_modulation_engine;

typedef struct {

	effects_engine base;				// included to imitate a supertype
	low_freq_oscillator lfo;			// A low frequency oscillator, to modulate the delay time
	onepole* apf_array[PHS_NUM_ALLPASS];// The array of 1st order allpass filters
	float phs_num_filters;				// size of the apf_array

	float apf_centre;					// the centre frequency of the allpass filters. The LFO modulates the centre frequency around this value
	float depth_octaves;				// used to modulate the centre freq such that it remains symmetrical in terms of pitch rather than Hz
	float dry_gain;						// Dry level
	float wet_gain;						// Wet level
	int bypass;	

	int sample_rate;

} phaser_modulation_engine;

void chorus_modulation_init(chorus_modulation_engine* m_engine, int sample_rate);
void process_chorus(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);

void flanger_modulation_init(flanger_modulation_engine* m_engine, int sample_rate);
void process_flanger(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);

void phaser_modulation_init(phaser_modulation_engine* m_engine, int sample_rate);
void process_phaser(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);

// chorus control functions
float set_chorus_rate(effects_engine* engine, float rate);
float set_chorus_depth(effects_engine* engine, float depth_ms);
float set_chorus_wet(effects_engine* engine, float wet);
float set_chorus_bypass(effects_engine* engine, float bypass);

// flanger control functions
float set_flanger_rate(effects_engine* engine, float rate);
float set_flanger_depth(effects_engine* engine, float depth_ms);
float set_flanger_feedback(effects_engine* engine, float feedback);
float set_flanger_feedforward(effects_engine* engine, float feedforward);
float set_flanger_dry(effects_engine* engine, float dry);
float set_flanger_hpf(effects_engine* engine, float cutoff);
float set_flanger_bypass(effects_engine* engine, float bypass);

// phaser control functions
float set_phaser_rate(effects_engine* engine, float rate);
float set_phaser_depth(effects_engine* engine, float depth);
float set_phaser_dry(effects_engine* engine, float dry);
float set_phaser_wet(effects_engine* engine, float wet);
float set_phaser_filters(effects_engine* engine, float num_filt);
float set_phaser_bypass(effects_engine* engine, float bypass);

#endif