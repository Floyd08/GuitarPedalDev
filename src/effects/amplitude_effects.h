#ifndef AMPLITUDE_FX_H
#define AMPLITUDE_FX_H

#include "effects_engine.h"
#include "dsp_tools.h"

// classic tremolo constants
#define		MIN_TRM_RATE		0.1f	// in Hz
#define		MAX_TRM_RATE		5.0f
#define		DFLT_TRM_RATE		1.0f
#define		DFLT_TRM_RATE_SET	0.3f

#define		MIN_TRM_DEPTH		0.0f	// The control works in reverse though
#define		MAX_TRM_DEPTH		1.0f	// clockwise increased the volume cut, anti-clockwise decreases it
#define		DFLT_TRM_DEPTH		0.4f	// All the way to the left turns off the effect
#define		DFLT_TRM_DEPTH_SET	0.4f

#define		MIN_TRM_WET			0.1f	// linear <= 1
#define		MAX_TRM_WET			1.0f	
#define		DFLT_TRM_WET		1.0f	
#define		DFLT_TRM_WET_SET	1.0f

// harmonic tremolo constants
#define		HARM_TRM_CENTRE		500

// enumerate amplitude effect controls
// classic tremolo controls
enum {
	TRM_CTRL_RATE,
	TRM_CTRL_DEPTH,
	TRM_CTRL_WET,
	TRM_NUM_CONTROLS
};
extern const ControlDef tremolo_controls[TRM_NUM_CONTROLS];

typedef struct {

	effects_engine base;				// included to imitate a supertype
	low_freq_oscillator lfo;			// A low frequency oscillator, to modulate the volume

	float rate;							// In Hz
	float depth;						// in dB
	float wet;							// straight control factor
	int sample_rate;

} tremolo_amplitude_engine;

typedef struct {

	effects_engine base;				// included to imitate a supertype
	low_freq_oscillator lfo;			// A low frequency oscillator, to modulate the volume
	biquad* lpf1;						// first half of a linkwitz riley low pass filter
	biquad* lpf2;						// second half of the LR filter

	float rate;							// In Hz
	float depth;						// in dB
	float wet;							// straight control factor
	int sample_rate;

} harmonic_trem_amplitude_engine;

// classic tremolo functions
void tremolo_amplitude_init(tremolo_amplitude_engine* a_engine, int sample_rate);
void process_tremolo(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);

// harmonic tremolo functions
void harmonic_tremolo_amplitude_init(harmonic_trem_amplitude_engine* a_engine, int sample_rate);
void process_harmonic_tremolo(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);

// classic tremollo control functions
void set_tremolo_rate(effects_engine* engine, float rate);
void set_tremolo_depth(effects_engine* engine, float depth);
void set_tremolo_wet(effects_engine* engine, float wet);

// harmonic tremollo control functions
void set_harmonic_trem_rate(effects_engine* engine, float rate);
void set_harmonic_trem_depth(effects_engine* engine, float depth);
void set_harmonic_trem_wet(effects_engine* engine, float wet);

#endif