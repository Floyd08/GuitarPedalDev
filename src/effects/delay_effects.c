#include <stdlib.h>
#include <stdio.h>
#include "delay_effects.h"

const ControlDef delay_controls[DLY_NUM_CONTROLS] = {
	[DLY_CTRL_TIME] = {.name = "delay", .label = "Delay", .type = CTRL_KNOB, .row = 1, .default_value = DFLT_DELAY, .update_func = set_delay_time },
	[DLY_CTRL_WET] = {.name = "delay_wet", .label = "Dry/Wet", .type = CTRL_KNOB, .row = 1, .default_value = DFLT_WET_MIX, .update_func = set_wet_mix },
	[DLY_CTRL_ECHO] = {.name = "echo", .label = "Echo", .type = CTRL_KNOB, .row = 1, .default_value = DFLT_DLY_GAIN, .update_func = set_feedback_gain },
};

void delay_engine_init(delay_engine* dl_engine, int sample_rate, int buffer_size) {

	effects_engine base;
	effects_engine_init(&base, "delay");
	base.controls = delay_controls;
	base.num_controls = DLY_NUM_CONTROLS;
	dl_engine->base = base;

	// ring_buffer r_buf;
	// r_buffer_init(&r_buf, DFLT_BUFFER_SIZE);
	// dl_engine->r_buf = r_buf;
	r_buffer_init(&dl_engine->r_buf, DFLT_BUFFER_SIZE);
	dl_engine->low_pass = bq_new(LOWPASS, 3000, 0.7071, -3, sample_rate);
	
	set_delay_time(&dl_engine->base, DFLT_DELAY);
	set_feedback_gain(&dl_engine->base, DFLT_DLY_GAIN);
	set_wet_mix(&dl_engine->base, DFLT_WET_MIX);
	
	dl_engine->delay_scratch = calloc(buffer_size, sizeof(float));
	dl_engine->sample_rate = sample_rate;

}

void process_delay(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

	delay_engine* dl_engine = (delay_engine*)engine;

	float* scratch = dl_engine->delay_scratch;

	r_buffer_read_delayed_bloc(&(dl_engine->r_buf), buffer_size, scratch, dl_engine->delay_time_samples);
	// r_buffer_write_block(&(dl_engine->r_buf), buffer_size, in_buf);											// used for a single repeat

	for(int i = 0; i < buffer_size; i++) {

		float dry = in_buf[i];
		float wet = scratch[i];
		wet = bq_process(dl_engine->low_pass, wet);

		r_buffer_write( &(dl_engine->r_buf), dry + dl_engine->feedback_gain * wet );							// write the echo to the rign buffer
		out_buf[i] = dry * (1.0f - dl_engine->wet_mix) + wet * dl_engine->wet_mix;								// write the output
		// out_buf[i] = (scratch[i] * dl_engine->wet_mix) + (in_buf[i]); 										// used for a single delay
	}

}

int ms_to_samples(int time_ms, int sample_rate) {

	int time_in_samples = (sample_rate / 1000) * time_ms;
	return time_in_samples;
}

void set_delay_time(effects_engine* engine, float time) {
	
	delay_engine* dl_engine = (delay_engine*)engine;

	unsigned int new_time = time * (MAX_DELAY - MIN_DELAY) + MIN_DELAY;			// the float is cast down to an int here, to avoid factional samples
	dl_engine->delay_time_ms = new_time;
	dl_engine->delay_time_samples = ms_to_samples(new_time, dl_engine->sample_rate);
}

void set_wet_mix(effects_engine* engine, float wet_mix) {

	delay_engine* dl_engine = (delay_engine*)engine;

	dl_engine->wet_mix = wet_mix;
}

void set_feedback_gain(effects_engine* engine, float gain) {

	delay_engine* dl_engine = (delay_engine*)engine;

	float new_feedback = linear_to_log(gain, minus_12dB, _0dB);					// sets a logarithmic range for feedback gain
	dl_engine->feedback_gain = new_feedback;
}