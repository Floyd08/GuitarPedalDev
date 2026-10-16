#ifndef DSP_TOOLS_H
#define DSP_TOOLS_H

#include "Biquad.h"
//#include "distortion_effects.h"

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

void envelope_follower_init(envelope_follower* e_fol, float attack_t, float release_t, int sample_rate);
void process_envelope_follower(envelope_follower* e_fol, float sample);
void set_env_attack(envelope_follower* e_fol, float new_attack_time, int sample_rate);
void set_env_release(envelope_follower* e_fol, float new_release_time, int sample_rate);

biquad* alias_init(int upsample_factor, int sample_rate);
void alias_terminate(biquad* alias_filter);
void alias_filter_process(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);
void alias_filter_process_gc(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);

void upsample(float* input_buf, int buffer_size, float* upsampled_buf, int upsample_factor);
void downsample_mono(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);
void downsample_stereo(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);

float linear_to_log(float linear_val, float min, float max);

#endif