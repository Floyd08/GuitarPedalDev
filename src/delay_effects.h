#ifndef DELAY_ENGINE_H
#define DELAY_ENGINE_H

#include "effects_engine.h"
#include "dsp_tools.h"

#define		MIN_DELAY			80			// the minimum delay in milliseconds
#define		MAX_DELAY			800			// the maximum delay in milliseconds
#define		DFLT_BUFFER_SIZE	65536		// assumes a sample rate of 48,000/s
#define		DFLT_DELAY			0.334		// default delay set at start up
#define		DFLT_DLY_GAIN		0.5			// default feedback gain for the echo
#define 	DFLT_WET_MIX		0.5			//  1 - 0.5 = 50/50 wet/dry

// enumerate delay controls
enum {
	DLY_CTRL_TIME,
	DLY_CTRL_WET,
	DLY_CTRL_ECHO,
	DLY_NUM_CONTROLS
};
extern const ControlDef delay_controls[DLY_NUM_CONTROLS];

typedef struct {

	effects_engine base;				// included to imitate a supertype
	ring_buffer r_buf;					// the ring buffer
	biquad* low_pass;					// a low pass filter for the echo
	int delay_time_ms;					// delay time in ms
	int delay_time_samples;				// delay time in samples
	float feedback_gain;				// the gain factor applied to each delayed sample
	float wet_mix;						// Used to apply wet/dry mix
	float *delay_scratch;				// scratch space for delay processing
	int sample_rate;

} delay_engine;

void delay_engine_init(delay_engine* dl_engine, int sample_rate, int buffer_size);
void process_delay(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);

void set_delay_time(effects_engine* engine, float time);
void set_wet_mix(effects_engine* engine, float wet_mix);
void set_feedback_gain(effects_engine* engine, float gain);

int ms_to_samples(int time_ms, int sample_rate);

#endif