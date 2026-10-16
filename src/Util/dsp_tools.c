#include <stdio.h>
#include <math.h>
#include "Biquad.h"
//#include "distortion_effects.h"

#define	BUTTERWORTH_Q				0.7071
#define UNITY_GAIN					0.0f
#define ALIAS_CUTOFF				20000

typedef struct {

	float state;
	float attack_time_ms;
	float attack_alpha;
	float release_time_ms;
	float release_alpha;
	
} envelope_follower;

void envelope_follower_init(envelope_follower* e_fol, float attack_t, float release_t, int sample_rate) {

	e_fol->state = 0;
	e_fol->attack_time_ms = attack_t;
	e_fol->attack_alpha = expf( -1.0f / (sample_rate * 0.001f * attack_t) );
	e_fol->release_time_ms = release_t;
	e_fol->release_alpha = expf( -1.0f / (sample_rate * 0.001f * release_t) );
}

void process_envelope_follower(envelope_follower* e_fol, float sample) {

	float level = fabsf(sample);
	float attack = e_fol->attack_alpha;
	float release = e_fol->release_alpha;
	float state = e_fol->state;

	// calculate new state
	if (level > state) {
		//adjust state with attack coefficient
		state = attack * state + (1.0f - attack) * level;
	}
	else {
		//adjust state with release coefficient
		state = release * state + (1.0f - release) * level;
	}

	//update the envelope state
	e_fol->state = state;
}

void set_env_attack(envelope_follower* e_fol, float new_attack_time, int sample_rate) {
	new_attack_time = fmaxf(new_attack_time, 0.1f);
	e_fol->attack_alpha = expf(-1.0f / (new_attack_time * 0.001f * sample_rate));
}

void set_env_release(envelope_follower* e_fol, float new_release_time, int sample_rate) {
	new_release_time = fmaxf(new_release_time, 0.1f);
	e_fol->release_alpha = expf(-1.0f / (new_release_time * 0.001f * sample_rate));
}

biquad* alias_init(int upsample_factor, int sample_rate) {
	
	int oversampled_rate = sample_rate * upsample_factor;
	biquad* alias_filter = bq_new(LOWPASS, ALIAS_CUTOFF, BUTTERWORTH_Q, UNITY_GAIN, oversampled_rate);
	return alias_filter;
}

void alias_terminate(biquad* alias_filter) {
	bq_destroy(alias_filter);
}

/*
*	@brief accepts an input buffer, and an output buffer scaled for the upsampling factor
*/
void upsample(float* input_buf, int buffer_size, float* upsampled_buf, int upsample_factor) {
	
	int i = 0, j = 0;
	while (i < buffer_size) {

		upsampled_buf[j] = input_buf[i];
		j++;

		int k = 1;
		while (k < upsample_factor) {

			upsampled_buf[j] = 0;
			j++;
			k++;
		}
		
		i++;
	}
}

void downsample_mono(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size) {

	int i = 0, j = 0; 							//up_size = buffer_size * upsample_factor;
	while (i < buffer_size) {

		output_buf[i] = upsampled_buf[j];
		i++;
		j = j + upsample_factor;
	}
}

void downsample_stereo(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size) {

	int i = 0, j = 0; 							//up_size = buffer_size * upsample_factor;
	while (i < (buffer_size*2)) {

		output_buf[i++] = upsampled_buf[j];
		output_buf[i++] = upsampled_buf[j];
		j = j + upsample_factor;
	}
}


//runs the upsampled buffer through a low pass filter configured for anti-aliasing
void alias_filter_process(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor) {

	for(int i = 0; i < (buffer_size * upsample_factor); i++) {
		//printf("Filter loop: %d\n", i);
		upsampled_buffer[i] = bq_process(alias_filter, upsampled_buffer[i]);
	}
}

//runs the upsampled buffer through a low pass filter configured for anti-aliasing, then applies gain compensation
void alias_filter_process_gc(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor) {

	for(int i = 0; i < (buffer_size * upsample_factor); i++) {
		//printf("Filter loop: %d\n", i);
		float filtered_sample = bq_process(alias_filter, upsampled_buffer[i]);

		//compensate gain
		upsampled_buffer[i] = filtered_sample * upsample_factor;
	}
}

float linear_to_log(float linear_val, float min, float max) {
    
	return min * powf(max / min, linear_val);
}
