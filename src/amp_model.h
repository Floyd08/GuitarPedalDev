#ifndef AMP_MODEL_H
#define AMP_MODEL_H

#include "dsp_tools.h"
#include "effects_engine.h"
#include "one_pole.h"
#include "distortion_effects.h"

#define		DFLT_BASS_FREQ			200
#define		DFLT_TREBLE_FREQ		3000
#define		MIN_TS_GAIN				-12
#define		MAX_TS_GAIN				6

#define		DFLT_MID_FREQ			800
#define		DFLT_MID_FREQ_LN		0.5
#define		MAX_MID_CENTRE			2400
#define		MIN_MID_CENTRE			150

#define		DFLT_GAIN_dB			-3
#define		DFLT_GAIN_LINEAR		0.5
#define		OVERSAMPLING_FACTOR		8

#define		POWER_AMP_ATTACK_MS		20
#define		POWER_AMP_RELEASE_MS	200
#define		RAIL_DEPTH				0.3
#define		RAIL_KNEE				0.8				// 0 < rail_knee < 1
#define		RAIL_FLOOR				0.4

enum {
	AMP_CTRL_GAIN,
	AMP_CTRL_DRVE,
	AMP_CTRL_BASS,
	AMP_CTRL_MID,
	AMP_CTRL_MID_SWP,
	AMP_CTRL_TRBL,
	AMP_NUM_CONTROLS
};
extern const ControlDef amp_model_controls[AMP_NUM_CONTROLS];

typedef struct {

	onepole* bass_lsf;				// low shelf filter, 200Hz, butterworth, unity
	float bass_gain;				// bass gain setting

	onepole* mid_peak;				// mid peaking filter, 800Hz, butterworth, unity
	float mid_gain;					// mpf gain setting
	float mid_freq;					// mpf centre frequency setting

	onepole* treble_hsf;			// high shelf filter, 3000Hz, butterworth, unity
	float treble_gain;				// treble gain setting

} tone_stack;

typedef struct {

	float gain;						// input gain, runs into stage 1						-3 to 15dB
	float drive;					// gain applied after tone stack, runs into stage 2		-3 to 15dB
	onepole* input_hpf; 			// input filter, highpass, 60Hz, butterworth, unity
	onepole* interstage_hpf;		// interstage hpf, 150Hz, butterworth, unity
	onepole* interstage_lpf;		// interstage lpf, 7000Hz, butterworth, unity
	tone_stack tone;				// tone stack pointer
	float bias;						// pushes the signal positive or negative to acheive asymmetric clipping		0.15
	float bias_precomp;				// stage 2 bias precomputed
	biquad* image_filter;			// A lowpass filter, calibrated for antt-imaging
	biquad* alias_filter;			// A lowpass filter, calibrated for anti-aliasing -- filters have a state to themselves, so we can't use one filter for both steps
	float* oversampling_buffer;		// A pointer to a larger float buffer. It's size is buffer_size * up_factor

} pre_amp;

typedef struct {

	// Sag simulation
	envelope_follower e_fol;		// envelope state represents the the op-amps suuply rail
									// attack defines how quickly the rail drains, and release defines  the rail recharge. 
	float rail_depth;				// max fractional rail ddrop at sustained load
	float knee;						// knee as a fraction of the ceiling
	float rail_floor;				// a floor on the fail state, to keep it from collapsing to 0

} power_amp;

typedef struct {

	biquad* input_hpf;			// input highpass, 80Hz
	biquad* bass_res;			// resonance peaks
	biquad* mid_res;
	biquad* treble_res;
	biquad* output_lp1;			// cascaded lowpass filters, for a steeper roll off
	biquad* output_lp2;

} filter_cab;

typedef struct {

	effects_engine base;		// included to imitate a supertype
	pre_amp pr_amp;				// models gain staging, tone stack, and filtering
	power_amp pw_amp;			// models power amp sag, clipping based on simulated load
	filter_cab cab;				// A chain of biquad filters configured to simulate a guitar ampspeaker
	int sample_rate;			//sample rate of the callback function

} amp_model;

void tone_stack_init(tone_stack* tone, int sample_rate);
float process_tone_stack(tone_stack* tone, float sample);
void pre_amp_init(pre_amp* pre_amp, int sample_rate, int buffer_size);
void process_pre_amp(pre_amp* pre_amp, float *in_buf, float *out_buf, int buffer_size);
void power_amp_init(power_amp* p_amp, int sample_rate);
void process_power_amp(power_amp* p_amp, float *in_buf, float *out_buf, int buffer_size);
void filter_cab_init(filter_cab* cab, int sample_rate);
void process_filter_cab(filter_cab* cab, float *in_buf, float *out_buf, int buffer_size);

void amp_model_init(amp_model* a_mod, int sample_rate, int buffer_size);
void process_amp_model(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size);

void set_preamp_gain(effects_engine* engine, float gain);
void set_preamp_drive(effects_engine* engine, float gain);
void set_preamp_bass(effects_engine* engine, float bass);
void set_preamp_mid(effects_engine* engine, float mid);
void set_preamp_treble(effects_engine* engine, float treble);
void set_preamp_mid_centre(effects_engine* engine, float centre);

#endif