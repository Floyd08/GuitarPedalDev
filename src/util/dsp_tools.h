#ifndef DSP_TOOLS_H
#define DSP_TOOLS_H

#include "one_pole.h"
#include "Biquad.h"

#define		TWO_PI					6.28318530718f
#define		BUTTERWORTH_Q			0.7071
#define 	UNITY_GAIN				0.0f
#define 	ALIAS_CUTOFF			20000

#define		NUM_AIAA_FILTERS		4
#define		AI_AA_Q_VALUES			{ 0.5098f, 0.6013f, 0.9000f, 2.5629f }
#define		SMTHER_EPSILON			1e-3f

/*----------------------------------------------------------------------------------------------*/
// dB to linear gain conversions
#define		minus_60dB				0.001
#define		minus_40dB				0.0100
#define		minus_30dB				0.0316
#define		minus_21dB				0.0891
#define		minus_20dB				0.1000
#define		minus_15dB				0.1778
#define		minus_12dB				0.2512
#define		minus_9dB				0.3548
#define		minus_6dB				0.5012
#define		minus_3dB				0.7079
#define		_0dB					1.0000
#define		plus_3dB				1.4125
#define		plus_6dB				1.9953
#define		plus_9dB				2.8184
#define		plus_12dB				3.9811
#define		plus_15dB				5.6234
#define		plus_21dB				11.2202
#define		plus_30dB				31.6228

typedef struct {

	float state;
	float attack_time_ms;
	float attack_alpha;
	float release_time_ms;
	float release_alpha;
	
} envelope_follower;

typedef struct {

	float state;
	float attack_ms;
	float attack_alpha;
	float complement_coeff;

} smoother;

// a set of biquad filters, used to filter for imaging and aliasing
typedef struct {

	int num_filters;
	biquad* filters[NUM_AIAA_FILTERS];

	int sample_rate;

} ai_aa_filter;

typedef struct {

	float* r_buffer;			// the real buffer
	int write_head;				// the current write position in the buffer
	int read_head;				// the current read position in the buffer
	unsigned int size;			// the size in samples
	unsigned int mask;			// to facilitate wrap around at the end of the buffer

} ring_buffer;

typedef struct {

	double phase;				// current state of the LFO
	double phi_increment;		// rate_hz / sample_rate

} low_freq_oscillator;

typedef struct{

	float ap_delay_ms;					// the delay in milliseconds
	unsigned int ap_delay_samples;		// the delay in samples
	ring_buffer ap_buffer;				// the ring buffer, of size SAMPLES * size
	float ap_gain;						// the gain factor applied either to feedback or feed forward has a similar effect as in a delay 

} allpass_filter;

typedef struct {

	float vc_delay_ms;					// the delay in milliseconds
	unsigned int vc_delay_samples;		// the delay in samples
	ring_buffer vc_buffer;				// the ring buffer, of size SAMPLES * size
	float comb_gain;					// the gain factor applied to the fedback sample (use this later for decay)
	onepole* lpf;						// a low pass filter, used to dampen the reverb tone each cycle (unity gain, cutoff centre 10kHz, range 1.5kHz to 15kHz)

} verb_comb_filter;

void envelope_follower_init(envelope_follower* e_fol, float attack_t, float release_t, int sample_rate);
void process_envelope_follower(envelope_follower* e_fol, float sample);
void set_env_attack(envelope_follower* e_fol, float new_attack_time, int sample_rate);
void set_env_release(envelope_follower* e_fol, float new_release_time, int sample_rate);

void smoother_init(smoother* sm, float init_value, float attack_ms, int sample_rate);
void process_smoother(smoother* sm, float target_value);
void set_smoother_attack(smoother* sm, float attack_ms, int sample_rate);

int r_buffer_init(ring_buffer* r_buffer, unsigned int size);
void r_buffer_free(ring_buffer* buffer);

float r_buffer_read(ring_buffer* buffer);
void r_buffer_read_block(ring_buffer* buffer, int num_samples, float* out_buf);
float r_buffer_read_index(ring_buffer* buffer, int index);
void r_buffer_read_delayed_bloc(ring_buffer* buffer, int num_samples, float* out_buf, int delay_in_samples);

void r_buffer_write(ring_buffer* buffer, float sample);
void r_buffer_write_block(ring_buffer* buffer, int num_samples, float* input);
float r_buffer_write_index(ring_buffer* buffer, int index, float sample);


void low_freq_oscillator_init(low_freq_oscillator* lfo, float rate_Hz, int sample_rate);
float process_lfo(low_freq_oscillator* lfo);
void set_lfo_rate(low_freq_oscillator* lfo, float rate_Hz, int sample_rate);


int allpass_init(allpass_filter* apf, unsigned int length_ms, unsigned int dl_length, int sample_rate, float gain);
void allpass_free(allpass_filter* apf);
float process_allpass(allpass_filter* apf, float sample);
void ap_update_gain(allpass_filter* apf, float gain);


void verb_comb_init(verb_comb_filter* vcf, unsigned int length_ms, unsigned int dl_length, int sample_rate, float lpf_centre, float gain);
void verb_comb_free(verb_comb_filter* vcf);
float process_verb_comb(verb_comb_filter* vcf, float sample);
void verb_comb_update_gain(verb_comb_filter* vcf, float gain);


biquad* alias_init(int upsample_factor, int sample_rate);
void alias_free(biquad* alias_filter);
void alias_filter_process(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);
void alias_filter_process_gc(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);

void ai_aa_filter_init(ai_aa_filter* filter, int oversample_rate, int sample_rate);
float process_aiaa_filter(ai_aa_filter* filter, float sample);
// need a free fucntion for ai_aa_filters

void upsample(float* input_buf, int buffer_size, float* upsampled_buf, int upsample_factor);
void downsample_mono(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);
void downsample_stereo(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);


float convert_to_range_exp(float linear_val, float min, float max);
float convert_to_range(float value, float min, float max);
float convert_to_rotary(float control_sig, int num_positions);
float linear_gain_to_decibels(float linear);
float linear_interpolate(float fractional_index, float sample1, float sample2);

#endif