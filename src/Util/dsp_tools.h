#ifndef DSP_TOOLS_H
#define DSP_TOOLS_H

#include "Biquad.h"
//#include "distortion_effects.h"

#define		BUTTERWORTH_Q		0.7071
#define 	UNITY_GAIN			0.0f
#define 	ALIAS_CUTOFF		20000

/*----------------------------------------------------------------------------------------------*/
// dB to linear gain conversions
#define		minus_60dB			0.001
#define		minus_30dB			0.0316
#define		minus_21dB			0.0891
#define		minus_15dB			0.1778
#define		minus_12dB			0.2512
#define		minus_6dB			0.5012
#define		minus_3dB			0.7079
#define		_0dB				1
#define		plus_3dB			1.4125
#define		plus_6dB			2.8184
#define		plus_12dB			3.9811
#define		plus_15dB			5.6234
#define		plus_21dB			11.2202
#define		plus_30dB			31.6228

typedef struct {

	float state;
	float attack_time_ms;
	float attack_alpha;
	float release_time_ms;
	float release_alpha;
	
} envelope_follower;

typedef struct {

	float* r_buffer;			// the real buffer
	int write_head;				// the current write position in the buffer
	int read_head;				// the current read position in the buffer
	unsigned int size;			// the size in samples
	unsigned int mask;			// to facilitate wrap around at the end of the buffer

} ring_buffer;

void envelope_follower_init(envelope_follower* e_fol, float attack_t, float release_t, int sample_rate);
void process_envelope_follower(envelope_follower* e_fol, float sample);
void set_env_attack(envelope_follower* e_fol, float new_attack_time, int sample_rate);
void set_env_release(envelope_follower* e_fol, float new_release_time, int sample_rate);


int r_buffer_init(ring_buffer* r_buffer, unsigned int size);
void r_buffer_free(ring_buffer* buffer);

float r_buffer_read(ring_buffer* buffer);
void r_buffer_read_block(ring_buffer* buffer, int num_samples, float* out_buf);
float r_buffer_read_index(ring_buffer* buffer, unsigned int index);
void r_buffer_read_delayed_bloc(ring_buffer* buffer, int num_samples, float* out_buf, int delay_in_samples);

void r_buffer_write(ring_buffer* buffer, float sample);
void r_buffer_write_block(ring_buffer* buffer, int num_samples, float* input);
float r_buffer_write_index(ring_buffer* buffer, int index, float sample);


biquad* alias_init(int upsample_factor, int sample_rate);
void alias_terminate(biquad* alias_filter);
void alias_filter_process(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);
void alias_filter_process_gc(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);

void upsample(float* input_buf, int buffer_size, float* upsampled_buf, int upsample_factor);
void downsample_mono(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);
void downsample_stereo(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);

float linear_to_log(float linear_val, float min, float max);
float rectify(float sample);
float soft_rectify(float sample, float knee);

#endif