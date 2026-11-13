#include <math.h>
#include "amplitude_effects.h"

const ControlDef tremolo_controls[TRM_NUM_CONTROLS] = {
	[TRM_CTRL_RATE] = {.name = "trm_rate", .label = "Rate", .type = CTRL_KNOB, .row = AMPLITUDE_ROW_NUM, .default_value = DFLT_TRM_RATE_SET, .update_func = set_tremolo_rate, .units = "Hz" },
	[TRM_CTRL_DEPTH] = {.name = "trm_depth", .label = "Depth", .type = CTRL_KNOB, .row = AMPLITUDE_ROW_NUM, .default_value = DFLT_TRM_DEPTH_SET, .update_func = set_tremolo_depth, .units = "dB" },
	[TRM_CTRL_WET] = {.name = "trm_wet", .label = "Dry/Wet", .type = CTRL_KNOB, .row = AMPLITUDE_ROW_NUM, .default_value = DFLT_TRM_WET_SET, .update_func = set_tremolo_wet, .units = "" }

};

const ControlDef harmonic_tremolo_controls[TRM_NUM_CONTROLS] = {
	[TRM_CTRL_RATE] = {.name = "trm_rate", .label = "Rate", .type = CTRL_KNOB, .row = AMPLITUDE_ROW_NUM, .default_value = DFLT_TRM_RATE_SET, .update_func = set_harmonic_trem_rate, .units = "Hz" },
	[TRM_CTRL_DEPTH] = {.name = "trm_depth", .label = "Depth", .type = CTRL_KNOB, .row = AMPLITUDE_ROW_NUM, .default_value = DFLT_TRM_DEPTH_SET, .update_func = set_harmonic_trem_depth, .units = "dB" },
	[TRM_CTRL_WET] = {.name = "trm_wet", .label = "Dry/Wet", .type = CTRL_KNOB, .row = AMPLITUDE_ROW_NUM, .default_value = DFLT_TRM_WET_SET, .update_func = set_harmonic_trem_wet, .units = "" }

};

// classic tremolo functions
void tremolo_amplitude_init(tremolo_amplitude_engine* a_engine, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "tremolo");
	base.controls = tremolo_controls;
	base.num_controls = TRM_NUM_CONTROLS;
	base.colour = "#00FF7F";
	a_engine->base = base;

	low_freq_oscillator_init(&a_engine->lfo, DFLT_TRM_RATE, sample_rate);

	a_engine->depth = DFLT_TRM_DEPTH;
	a_engine->wet = DFLT_TRM_WET_SET;
	a_engine->sample_rate = sample_rate;
}

void process_tremolo(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

	tremolo_amplitude_engine* a_engine = (tremolo_amplitude_engine*) engine;

	float dry_sig, wet_sig, mod, gain, dry_level, output;

	for (int i = 0; i < buffer_size; i++) {

		dry_sig = in_buf[i];

		// compute the lfo, and use it to modulate gain
		mod = 1 - fabsf(process_lfo(&a_engine->lfo));
		gain = 1 - (a_engine->depth * mod);

		// apply the gain to the wet signal and mix with the dry
		dry_level = 1 - a_engine->wet;
		wet_sig = dry_sig * gain;

		output = (dry_level * dry_sig) + (a_engine->wet * wet_sig);
		out_buf[i] = output;
	}
}


void harmonic_tremolo_amplitude_init(harmonic_trem_amplitude_engine* a_engine, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "harmonic tremolo");
	base.controls = harmonic_tremolo_controls;
	base.num_controls = TRM_NUM_CONTROLS;
	base.colour = "#7FFF00";
	a_engine->base = base;

	low_freq_oscillator_init(&a_engine->lfo, DFLT_TRM_RATE, sample_rate);
	a_engine->lpf1 = bq_new(LOWPASS, HARM_TRM_CENTRE, BUTTERWORTH_Q, UNITY_GAIN, sample_rate);
	a_engine->lpf2 = bq_new(LOWPASS, HARM_TRM_CENTRE, BUTTERWORTH_Q, UNITY_GAIN, sample_rate);

	a_engine->depth = DFLT_TRM_DEPTH;
	a_engine->wet = DFLT_TRM_WET_SET;
	a_engine->sample_rate = sample_rate;
}

void process_harmonic_tremolo(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

	harmonic_trem_amplitude_engine* a_engine = (harmonic_trem_amplitude_engine*) engine;

	float dry_sig, wet_sig, bass, treble, mod, bass_gain, treble_gain, dry_level, output;

	for (int i = 0; i < buffer_size; i++) {

		// split the signal into bass and treble bands
		dry_sig = in_buf[i];
		bass = bq_process(a_engine->lpf1, dry_sig);
		bass = bq_process(a_engine->lpf2, bass);
		treble = dry_sig - bass;

		// compute the lfo, and use it to modulate gain
		mod = 1 - fabsf(process_lfo(&a_engine->lfo));
		treble_gain = (a_engine->depth * mod);
		bass_gain = 1 - treble_gain;

		// mix bass and treble bands into a wet signal
		wet_sig = (bass * bass_gain) + (treble * treble_gain);

		// apply the gain to the wet signal and mix with the dry
		dry_level = 1 - a_engine->wet;

		output = (dry_level * dry_sig) + (a_engine->wet * wet_sig);
		out_buf[i] = output;
	}
}

// classic tremollo control functions
float set_tremolo_rate(effects_engine* engine, float rate) {

	tremolo_amplitude_engine* a_engine = (tremolo_amplitude_engine*) engine;

	float new_rate = convert_to_range(rate, MIN_TRM_RATE, MAX_TRM_RATE);

	set_lfo_rate(&a_engine->lfo, new_rate, a_engine->sample_rate);

	return new_rate;
}

float set_tremolo_depth(effects_engine* engine, float depth) {

	tremolo_amplitude_engine* a_engine = (tremolo_amplitude_engine*) engine;

	float new_depth = convert_to_range(depth, MIN_TRM_DEPTH, MAX_TRM_DEPTH);

	a_engine->depth = new_depth;

	return new_depth;
}

float set_tremolo_wet(effects_engine* engine, float wet) {

	tremolo_amplitude_engine* a_engine = (tremolo_amplitude_engine*) engine;

	a_engine->wet = wet;

	return wet;
}

// classic tremollo control functions
float set_harmonic_trem_rate(effects_engine* engine, float rate) {

	harmonic_trem_amplitude_engine* a_engine = (harmonic_trem_amplitude_engine*) engine;

	float new_rate = convert_to_range(rate, MIN_TRM_RATE, MAX_TRM_RATE);

	set_lfo_rate(&a_engine->lfo, new_rate, a_engine->sample_rate);

	return new_rate;
}

float set_harmonic_trem_depth(effects_engine* engine, float depth) {

	harmonic_trem_amplitude_engine* a_engine = (harmonic_trem_amplitude_engine*) engine;

	float new_depth = convert_to_range(depth, MIN_TRM_DEPTH, MAX_TRM_DEPTH);

	a_engine->depth = new_depth;

	return new_depth;
}

float set_harmonic_trem_wet(effects_engine* engine, float wet) {

	harmonic_trem_amplitude_engine* a_engine = (harmonic_trem_amplitude_engine*) engine;

	a_engine->wet = wet;

	return wet;
}