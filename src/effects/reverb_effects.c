#include <math.h>
#include "reverb_effects.h"
#include "distortion_effects.h"

const ControlDef series_ap_reverb_controls[S5_RVRB_NUM_CONTROLS] = {
	[S5_RVRB_CTRL_VERB] = {.name = "verb", .label = "Verb", .type = CTRL_KNOB, .row = 5, .default_value = DFLT_VERB_WET, .update_func = set_sap_rvrb_wet_mix},
	[S5_RVRB_CTRL_DECAY] = {.name = "decay", .label = "Decay", .type = CTRL_KNOB, .row = 5, .default_value = DFLT_VERB_GAIN, .update_func = set_sap_rvrb_decay}
};
const ControlDef freeverb_reverb_controls[FREEVERB_NUM_CONTROLS] = {
	[FREEVERB_CTRL_VERB] = {.name = "verb", .label = "Verb", .type = CTRL_KNOB, .row = 5, .default_value = DFLT_VERB_WET, .update_func = set_freeverb_wet_mix},
	[FREEVERB_CTRL_DECAY] = {.name = "decay", .label = "Decay", .type = CTRL_KNOB, .row = 5, .default_value = DFLT_COMB_GAIN, .update_func = set_freeverb_decay},
	[FREEVERB_CTRL_TONE] = {.name = "freeverb_tone", .label = "Tone", .type = CTRL_KNOB, .row = 5, .default_value = DFLT_LPF_TONE, .update_func = set_freeverb_lpf_freq}
};

// series allpass reverb
void series_ap_reverb_engine_init(series_ap_reverb_engine* r_engine, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "series allpass reverb");
	base.controls = series_ap_reverb_controls;
	base.num_controls = S5_RVRB_NUM_CONTROLS;
	r_engine->base = base;

	allpass_init(&r_engine->ap_array[4], SERIES_AP_5_MS, DFLT_DL_SIZE, sample_rate, DFLT_VERB_GAIN);
	allpass_init(&r_engine->ap_array[3], SERIES_AP_4_MS, DFLT_DL_SIZE, sample_rate, DFLT_VERB_GAIN);
	allpass_init(&r_engine->ap_array[2], SERIES_AP_3_MS, DFLT_DL_SIZE, sample_rate, DFLT_VERB_GAIN);
	allpass_init(&r_engine->ap_array[1], SERIES_AP_2_MS, DFLT_DL_SIZE, sample_rate, DFLT_VERB_GAIN);
	allpass_init(&r_engine->ap_array[0], SERIES_AP_1_MS, DFLT_DL_SIZE, sample_rate, DFLT_VERB_GAIN);
	
	set_sap_rvrb_decay(&r_engine->base, DFLT_VERB_GAIN);
	set_sap_rvrb_wet_mix(&r_engine->base, DFLT_VERB_WET);
}

void process_series_ap_reverb(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

	series_ap_reverb_engine* r_engine = (series_ap_reverb_engine*)engine;

	float dry_signal, wet_signal;
	for(int i = 0; i < buffer_size; i++) {
		
		dry_signal = in_buf[i];
		wet_signal = dry_signal;
		for(int j = 0; j < SERIES_AP_NUM_ALLPASS; j++) {
			
			wet_signal = process_allpass(&r_engine->ap_array[j], wet_signal);
		}
		out_buf[i] = (wet_signal * r_engine->wet_gain) + (dry_signal *  r_engine->dry_gain);
	}
}

void set_sap_rvrb_decay(effects_engine* engine, float decay) {

	series_ap_reverb_engine* r_engine = (series_ap_reverb_engine*)engine;

	float new_decay;
	new_decay = convert_to_range(decay, MIN_VERB_GAIN, MAX_VERB_GAIN);
	fprintf(stderr, "new_decay: %f\n", new_decay);
	for(int i = 0; i < SERIES_AP_NUM_ALLPASS; i++) {

		ap_update_gain(&r_engine->ap_array[i], new_decay);
	}
}

void set_sap_rvrb_wet_mix(effects_engine* engine, float wet_mix) {

	series_ap_reverb_engine* r_engine = (series_ap_reverb_engine*)engine;

	float dry_gain, wet_gain;
	wet_gain = MAX_VERB_WET * wet_mix;
	dry_gain = MAX_VERB_DRY * (1 - wet_mix);
	fprintf(stderr, "wet_gain: %f, dry_gain: %f\n", wet_gain, dry_gain);
	r_engine->wet_gain = wet_gain;
	r_engine->dry_gain = dry_gain;
}

// freeverb reverb
void freeverb_reverb_engine_init(freeverb_reverb_engine* r_engine, int sample_rate) {

	effects_engine base;
	effects_engine_init(&base, "freeverb reverb");
	base.controls = freeverb_reverb_controls;
	base.num_controls = FREEVERB_NUM_CONTROLS;
	r_engine->base = base;

	float comb_ms[FREEVERB_NUM_COMBS] = FREEVERB_CB_MS;
	float comb_gain, samples_per_ms = sample_rate / 1000;
	for (int i = 0; i < FREEVERB_NUM_COMBS; i++) {

		comb_gain = powf( 10, -3 * ( (comb_ms[i] * samples_per_ms) / (DFLT_COMB_GAIN * r_engine->sample_rate) ) );
		verb_comb_init(&r_engine->comb_array[i], comb_ms[i], DFLT_DL_SIZE, sample_rate, COMB_LPF_DFLT_CUTOFF, comb_gain);
	}

	float allpass_ms[FREEVERB_NUM_ALLPASS] = FREEVERB_AP_MS;
	for (int i = 0; i < FREEVERB_NUM_ALLPASS; i++) {
		allpass_init(&r_engine->ap_array[i], allpass_ms[i], DFLT_DL_SIZE, sample_rate, DFLT_AP_GAIN);
	}

	r_engine->wet_gain = 1;
	r_engine->dry_gain = 0;
	r_engine->sample_rate = sample_rate;
}

void process_freeverb(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

	freeverb_reverb_engine* r_engine = (freeverb_reverb_engine*)engine;

	int n;
	float sample, sample_scratch, output, comb_accumulator;

	for (int i = 0; i < buffer_size; i++) {

		comb_accumulator = 0.0f;
		sample = in_buf[i];
		sample_scratch = sample * 0.03f;
		
		// process the 8 comb filters, and sum their outputs
		n = 0;
		while (n < FREEVERB_NUM_COMBS) {

			comb_accumulator = comb_accumulator + process_verb_comb(&r_engine->comb_array[n], sample_scratch);
			n++;
		}
		sample_scratch = comb_accumulator;

		// run the output of the comb filters through the 4 Allpass filters
		n = 0;
		while (n < FREEVERB_NUM_ALLPASS) {

			sample_scratch = process_allpass(&r_engine->ap_array[n], sample_scratch);
			n++;
		}
		output = (sample * r_engine->dry_gain) + (sample_scratch * r_engine->wet_gain);
		output = soft_knee_distortion(output);
		out_buf[i] = output;
	}

}

void set_freeverb_wet_mix(effects_engine* engine, float wet_mix) {

	freeverb_reverb_engine* r_engine = (freeverb_reverb_engine*)engine;

	float dry_gain, wet_gain;
	wet_gain = MAX_VERB_WET * wet_mix;
	dry_gain = MAX_VERB_DRY * (1 - wet_mix);
	// fprintf(stderr, "wet_gain: %f, dry_gain: %f\n", wet_gain, dry_gain);
	r_engine->wet_gain = wet_gain;
	r_engine->dry_gain = dry_gain;
}

void set_freeverb_decay(effects_engine* engine, float decay) {

	freeverb_reverb_engine* r_engine = (freeverb_reverb_engine*)engine;

	float new_decay, new_gain, samples_per_ms;
	float comb_ms[FREEVERB_NUM_COMBS] = FREEVERB_CB_MS;
	
	new_decay = convert_to_range(decay, MIN_RT60_S, MAX_RT60_S);
	samples_per_ms = r_engine->sample_rate / 1000;
	// fprintf(stderr, "new_decay: %f\n", new_decay);
	for(int i = 0; i < FREEVERB_NUM_COMBS; i++) {

		new_gain = powf( 10, -3 * ( (comb_ms[i] * samples_per_ms) / (new_decay * r_engine->sample_rate) ) );
		verb_comb_update_gain(&r_engine->comb_array[i], new_gain);
	}
}

void set_freeverb_lpf_freq(effects_engine* engine, float cutoff) {

	freeverb_reverb_engine* r_engine = (freeverb_reverb_engine*)engine;

	float new_cutoff;
	new_cutoff = convert_to_range(cutoff, COMB_LPF_MIN, COMB_LPF_MAX);
	// fprintf(stderr, "new cutoff: %f\n", new_cutoff);
	for(int i = 0; i < FREEVERB_NUM_COMBS; i++) {
	
		op_update(r_engine->comb_array[i].lpf, LOWPASS, new_cutoff, UNITY_GAIN, r_engine->sample_rate);
	}
}