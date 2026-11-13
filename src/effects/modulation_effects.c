#include <math.h>
#include <stdio.h>
#include "modulation_effects.h"

const ControlDef chorus_controls[CHRS_NUM_CONTROLS] = {
	[CHRS_CTRL_RATE] = {.name = "ch_rate", .label = "Rate", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_CH_RATE_SET, .update_func = set_chorus_rate, .units = "Hz" },
	[CHRS_CTRL_DEPTH] = {.name = "ch_depth", .label = "Depth", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_CH_DEPTH_SET, .update_func = set_chorus_depth, .units = "ms" },
	[CHRS_CTRL_WET] = {.name = "ch_wet", .label = "Dry/Wet", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_CH_WET, .update_func = set_chorus_wet, .units = "" },
	[CHRS_CTRL_BYPASS] = { .name = "ch_bypass", .label = "Bypass", .type = CTRL_SWITCH, .row = MODULATION_ROW_NUM, .default_value = OFF, .update_func = set_chorus_bypass, .units = "" }
};

const ControlDef flanger_controls[FLNG_NUM_CONTROLS] = {
	[FLNG_CTRL_RATE] = {.name = "flng_rate", .label = "Rate", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_FLNG_RATE_SET, .update_func = set_flanger_rate, .units = "Hz" },
	[FLNG_CTRL_DEPTH] = {.name = "flng_depth", .label = "Depth", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_FLNG_DEPTH_SET, .update_func = set_flanger_depth, .units = "ms" },
	[FLNG_CTRL_FDBK] = {.name = "flng_feedback", .label = "Feedback", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_FLNG_FDBK_SET, .update_func = set_flanger_feedback, .units = "" },
	[FLNG_CTRL_FRWD] = {.name = "flng_forward", .label = "Feedforward", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_FLNG_FRWD_SET, .update_func = set_flanger_feedforward, .units = "" },
	[FLNG_CTRL_BLND] = {.name = "flng_blend", .label = "Blend", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_FLNG_DRY_SET, .update_func = set_flanger_dry, .units = "" },
	[FLNG_CTRL_HPF] = {.name = "flng_tone", .label = "Tone", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_FLNG_HPF_SET, .update_func = set_flanger_hpf, .units = "Hz" },
	[FLNG_CTRL_BYPASS] = { .name = "flng_bypass", .label = "Bypass", .type = CTRL_SWITCH, .row = MODULATION_ROW_NUM, .default_value = OFF, .update_func = set_flanger_bypass, .units = "" }
};

const ControlDef phaser_controls[PHS_NUM_CONTROLS] = {
	[PHS_CTRL_RATE] = {.name = "phs_rate", .label = "Rate", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_PHS_RATE_SET, .update_func = set_phaser_rate, .units = "Hz" },
	[PHS_CTRL_DEPTH] = {.name = "phs_depth", .label = "Depth", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_PHS_DEPTH_SET, .update_func = set_phaser_depth, .units = "Octaves" },
	[PHS_CTRL_DRY] = {.name = "phs_dry", .label = "Dry", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_PHS_DRY_SET, .update_func = set_phaser_dry, .units = "" },
	[PHS_CTRL_WET] = {.name = "phs_wet", .label = "Wet", .type = CTRL_KNOB, .row = MODULATION_ROW_NUM	, .default_value = DFLT_PHS_WET_SET, .update_func = set_phaser_wet, .units = "" },
	[PHS_CTRL_FILTERS] = {.name = "phs_filters", .label = "Filters", .type = CTRL_ROTARY, .row = MODULATION_ROW_NUM	, .default_value = DFLT_NUM_FLTR_SET, .update_func = set_phaser_filters, .units = "", .positions = PHS_NUM_ALLPASS },
	[PHS_CTRL_BYPASS] = { .name = "phs_bypass", .label = "Bypass", .type = CTRL_SWITCH, .row = MODULATION_ROW_NUM, .default_value = OFF, .update_func = set_phaser_bypass, .units = "" }
};

void chorus_modulation_init(chorus_modulation_engine* m_engine, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "chorus");
	base.controls = chorus_controls;
	base.num_controls = CHRS_NUM_CONTROLS;
	base.colour = "#FF00FF";
	m_engine->base = base;

	float samples_per_ms = sample_rate / 1000;

	low_freq_oscillator_init(&m_engine->lfo, DFLT_CH_RATE, sample_rate);
	r_buffer_init(&m_engine->chorus_line, CH_BUFF_SIZE);
	m_engine->base_delay_ms = CH_BASE_DLY;
	m_engine->base_delay_samples = CH_BASE_DLY * samples_per_ms;

	//set_chorus_rate(&m_engine->base, DFLT_CH_RATE_SET);
	set_chorus_depth(&m_engine->base, DFLT_FLNG_DEPTH_SET);
	m_engine->wet_mix = DFLT_CH_WET;

	m_engine->sample_rate = sample_rate;
	m_engine->samples_per_ms = samples_per_ms;
	m_engine->bypass = OFF;
}

void flanger_modulation_init(flanger_modulation_engine* m_engine, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "flanger");
	base.controls = flanger_controls;
	base.num_controls = FLNG_NUM_CONTROLS;
	base.colour = "#9400D3";
	m_engine->base = base;

	float samples_per_ms = sample_rate / 1000;

	low_freq_oscillator_init(&m_engine->lfo, DFLT_FLNG_RATE, sample_rate);
	r_buffer_init(&m_engine->chorus_line, FLNG_BUFF_SIZE);
	m_engine->hpf = bq_new(HIGHPASS, DFLT_FLNG_HPF, BUTTERWORTH_Q, UNITY_GAIN, sample_rate);

	m_engine->base_delay_ms = FLNG_BASE_DLY;
	m_engine->base_delay_samples = FLNG_BASE_DLY * samples_per_ms;

	//set_flanger_rate(&m_engine->base, DFLT_FLNG_RATE_SET);
	set_flanger_depth(&m_engine->base, DFLT_FLNG_DEPTH_SET);
	set_flanger_feedback(&m_engine->base, DFLT_FLNG_FDBK_SET);
	set_flanger_feedforward(&m_engine->base, DFLT_FLNG_FRWD_SET);
	set_flanger_dry(&m_engine->base, DFLT_FLNG_DRY_SET);

	m_engine->sample_rate = sample_rate;
	m_engine->samples_per_ms = samples_per_ms;
	m_engine->bypass = OFF;
}

void phaser_modulation_init(phaser_modulation_engine* m_engine, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "phaser");
	base.controls = phaser_controls;
	base.num_controls = PHS_NUM_CONTROLS;
	base.colour = "#6A5ACD";
	m_engine->base = base;

	low_freq_oscillator_init(&m_engine->lfo, DFLT_PHS_RATE, sample_rate);

	m_engine->phs_num_filters = PHS_NUM_ALLPASS;
	for(int i = 0; i < m_engine->phs_num_filters; i++) {

		m_engine->apf_array[i] = op_new(OP_ALLPASS, DFLT_PHS_CENTRE, UNITY_GAIN, sample_rate);
	}

	m_engine->apf_centre = DFLT_PHS_CENTRE;
	set_phaser_depth(&m_engine->base, DFLT_PHS_DEPTH_SET);
	set_phaser_dry(&m_engine->base, DFLT_PHS_DRY_SET);
	set_phaser_wet(&m_engine->base, DFLT_PHS_WET_SET);

	m_engine->sample_rate = sample_rate;
	m_engine->bypass = OFF;
}

void process_chorus(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

	chorus_modulation_engine* m_engine = (chorus_modulation_engine*) engine;

	if (m_engine->bypass == 0) {
		return;
	}

	float sample, mod_delay, mod_delay_ms, mod_sample1, mod_sample2, mod_sample_interp, output;
	int mod_delay_samples;
	for (int i = 0; i < buffer_size; i++) {

		sample = in_buf[i];

		// compute the delay for this cycle, first in ms, then convert to samples per ms
		mod_delay_ms = m_engine->base_delay_ms + ( m_engine->depth_ms * process_lfo(&m_engine->lfo) );
		mod_delay = mod_delay_ms * m_engine->samples_per_ms;
		if (mod_delay < 1) {
			mod_delay = 1;
		}
		mod_delay_samples = (int) floorf(mod_delay);
		
		// read the delayed sample
		mod_sample1 = r_buffer_read_index(&m_engine->chorus_line, m_engine->chorus_line.write_head - mod_delay_samples);
		mod_delay_samples++;
		mod_sample2 = r_buffer_read_index(&m_engine->chorus_line, m_engine->chorus_line.write_head - mod_delay_samples);
		mod_sample_interp = linear_interpolate(mod_delay, mod_sample1, mod_sample2);

		// write dry signal to the ring buffer - for chorus
		r_buffer_write(&m_engine->chorus_line, sample);

		// dry/wet mix, and output
		output = (sample * (1 - m_engine->wet_mix)) + (mod_sample_interp * m_engine->wet_mix);
		out_buf[i] = output;
	}
}

void process_flanger(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

	flanger_modulation_engine* m_engine = (flanger_modulation_engine*) engine;

	if (m_engine->bypass == 0) {
		return;
	}

	float sample, mod_delay, mod_delay_ms, mod_sample1, mod_sample2, mod_sample_interp, output;
	int mod_delay_samples;
	for (int i = 0; i < buffer_size; i++) {

		sample = in_buf[i];

		// compute the delay for this cycle, first in ms, then convert to samples per ms
		mod_delay_ms = m_engine->base_delay_ms + ( m_engine->depth_ms * process_lfo(&m_engine->lfo) );
		mod_delay = mod_delay_ms * m_engine->samples_per_ms;
		if (mod_delay < 1) {
			mod_delay = 1;
		}
		mod_delay_samples = (int) floorf(mod_delay);
		
		// read the delayed sample
		mod_sample1 = r_buffer_read_index(&m_engine->chorus_line, m_engine->chorus_line.write_head - mod_delay_samples);
		mod_delay_samples++;
		mod_sample2 = r_buffer_read_index(&m_engine->chorus_line, m_engine->chorus_line.write_head - mod_delay_samples);
		mod_sample_interp = linear_interpolate(mod_delay, mod_sample1, mod_sample2);

		// write dry signal to the ring buffer - for chorus
		r_buffer_write(&m_engine->chorus_line, sample + mod_sample_interp * m_engine->feedback_gain);

		// blend the dry signal with the modulated signal. Much of the effect lives in how the Dry and Wet levels combine
		output = (sample * m_engine->blend) + (mod_sample_interp * m_engine->feedforward_gain);
		//output = (sample * (1 - m_engine->wet_mix)) + (mod_sample_interp * m_engine->wet_mix);
		out_buf[i] = output;
	}
}

void process_phaser(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

	phaser_modulation_engine* m_engine = (phaser_modulation_engine*) engine;

	if (m_engine->bypass == 0) {
		return;
	}

	float output, dry_sig, wet_sig, lfo_output, new_apf_centre;

	for(int i = 0; i < buffer_size; i++) {

		// get the input sample and hold on to it as the dry signal
		dry_sig = in_buf[i];
		wet_sig = dry_sig;
		// process the wet signal through the allpass filters
		for (int j = 0; j < m_engine->phs_num_filters; j++) {

			wet_sig = op_process(m_engine->apf_array[j], wet_sig);
		}
		
		// update the centre frequencies of the all pass filters
		lfo_output = process_lfo(&m_engine->lfo);
		new_apf_centre = m_engine->apf_centre * exp2f(m_engine->depth_octaves * lfo_output);
		for (int j = 0; j < m_engine->phs_num_filters; j++) {

			op_update(m_engine->apf_array[j], OP_ALLPASS, new_apf_centre, UNITY_GAIN, m_engine->sample_rate);
		}

		// Dry/Wet blend and output
		output = (dry_sig * m_engine->dry_gain) + (wet_sig * m_engine->wet_gain);
		out_buf[i] = output;
	}

}


// update functions for Chorus
float set_chorus_rate(effects_engine* engine, float rate) {

	chorus_modulation_engine* m_engine = (chorus_modulation_engine*) engine;

	float new_rate = convert_to_range(rate, MIN_CH_RATE, MAX_CH_RATE);

	set_lfo_rate(&m_engine->lfo, new_rate, m_engine->sample_rate);

	return new_rate;
}

float set_chorus_depth(effects_engine* engine, float depth_ms) {

	chorus_modulation_engine* m_engine = (chorus_modulation_engine*) engine;

	float new_depth = convert_to_range(depth_ms, MIN_CH_DEPTH, MAX_CH_DEPTH);

	m_engine->depth_ms = new_depth;
	m_engine->base_delay_samples = new_depth * m_engine->samples_per_ms;

	return new_depth;
}

float set_chorus_wet(effects_engine* engine, float wet) {

	chorus_modulation_engine* m_engine = (chorus_modulation_engine*) engine;

	m_engine->wet_mix = wet;

	return wet;
}

float set_chorus_bypass(effects_engine* engine, float bypass) {

	chorus_modulation_engine* m_engine = (chorus_modulation_engine*) engine;

	int new_bypass = (int)bypass;
	m_engine->bypass = new_bypass;

	return new_bypass;
}

// update functions for Flanger
float set_flanger_rate(effects_engine* engine, float rate) {

	flanger_modulation_engine* m_engine = (flanger_modulation_engine*) engine;

	float new_rate = convert_to_range(rate, MIN_FLNG_RATE, MAX_FLNG_RATE);

	set_lfo_rate(&m_engine->lfo, new_rate, m_engine->sample_rate);

	return new_rate;
}

float set_flanger_depth(effects_engine* engine, float depth_ms) {

	flanger_modulation_engine* m_engine = (flanger_modulation_engine*) engine;

	float new_depth = convert_to_range(depth_ms, MIN_FLNG_DEPTH, MAX_FLNG_DEPTH);

	m_engine->depth_ms = new_depth;
	m_engine->base_delay_samples = new_depth * m_engine->samples_per_ms;

	return new_depth;
}

float set_flanger_feedback(effects_engine* engine, float feedback) {

	flanger_modulation_engine* m_engine = (flanger_modulation_engine*) engine;

	float new_feedback = convert_to_range(feedback, MIN_FLNG_FDBK, MAX_FLNG_FDBK);

	m_engine->feedback_gain = new_feedback;

	return new_feedback;
}

float set_flanger_feedforward(effects_engine* engine, float feedforward) {

	flanger_modulation_engine* m_engine = (flanger_modulation_engine*) engine;

	float new_feedforward = convert_to_range(feedforward, MIN_FLNG_FRWD, MAX_FLNG_FRWD);

	m_engine->feedforward_gain = new_feedforward;

	return new_feedforward;
}

float set_flanger_dry(effects_engine* engine, float dry) {

	flanger_modulation_engine* m_engine = (flanger_modulation_engine*) engine;

	float new_dry = convert_to_range(dry, MIN_FLNG_DRY, MAX_FLNG_DRY);

	m_engine->blend = new_dry;

	return new_dry;
}

float set_flanger_hpf(effects_engine* engine, float cutoff) {

	flanger_modulation_engine* m_engine = (flanger_modulation_engine*) engine;

	float new_cutoff = convert_to_range(cutoff, MIN_FLNG_HPF, MAX_FLNG_HPF);

	bq_update(m_engine->hpf, HIGHPASS, new_cutoff, BUTTERWORTH_Q, UNITY_GAIN, m_engine->sample_rate);

	return new_cutoff;
}

float set_flanger_bypass(effects_engine* engine, float bypass) {

	flanger_modulation_engine* m_engine = (flanger_modulation_engine*) engine;

	int new_bypass = (int)bypass;
	m_engine->bypass = new_bypass;

	return new_bypass;
}

// update functions for Phaser
float set_phaser_rate(effects_engine* engine, float rate) {

	phaser_modulation_engine* m_engine = (phaser_modulation_engine*) engine;

	float new_rate = convert_to_range(rate, MIN_PHS_RATE, MAX_PHS_RATE);

	set_lfo_rate(&m_engine->lfo, new_rate, m_engine->sample_rate);

	return new_rate;
}

float set_phaser_depth(effects_engine* engine, float depth) {

	phaser_modulation_engine* m_engine = (phaser_modulation_engine*) engine;

	float new_depth = convert_to_range(depth, MIN_PHS_DEPTH, MAX_PHS_DEPTH);

	m_engine->depth_octaves = new_depth;

	return new_depth;
}

float set_phaser_dry(effects_engine* engine, float dry) {

	phaser_modulation_engine* m_engine = (phaser_modulation_engine*) engine;

	float normal, new_dry = convert_to_range(dry, MIN_PHS_DRY, MAX_PHS_DRY);

	normal = new_dry + m_engine->wet_gain;
	m_engine->dry_gain = new_dry / normal;
	m_engine->wet_gain = m_engine->wet_gain / normal;

	return new_dry;
}

float set_phaser_wet(effects_engine* engine, float wet) {

	phaser_modulation_engine* m_engine = (phaser_modulation_engine*) engine;

	float normal, new_wet = convert_to_range(wet, MIN_PHS_WET, MAX_PHS_WET);

	normal = new_wet + m_engine->dry_gain;
	m_engine->wet_gain = new_wet / normal;
	m_engine->dry_gain = m_engine->dry_gain / normal;

	return new_wet;
}

float set_phaser_filters(effects_engine* engine, float num_filt) {

	phaser_modulation_engine* m_engine = (phaser_modulation_engine*) engine;

	float new_num_filt = convert_to_rotary(num_filt, PHS_NUM_ALLPASS);
	m_engine->phs_num_filters = new_num_filt;

	return new_num_filt;
}

float set_phaser_bypass(effects_engine* engine, float bypass) {

	phaser_modulation_engine* m_engine = (phaser_modulation_engine*) engine;

	int new_bypass = (int)bypass;
	m_engine->bypass = new_bypass;

	return new_bypass;
}