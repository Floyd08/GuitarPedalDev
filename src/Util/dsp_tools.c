#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Biquad.h"
#include "dsp_tools.h"


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


int r_buffer_init(ring_buffer* r_buffer, unsigned int size) {

	if (size < 2) {
		fprintf(stderr, "ring_buffer size must be 2 or greater\n");
		return 1;
	}
	if ((size & (size - 1)) != 0) {
		fprintf(stderr, "ring_buffer size must be a power of 2\n");
		return 1;
	}

	r_buffer->r_buffer = calloc(size, sizeof(float));
	if(r_buffer->r_buffer == NULL) {
		fprintf(stderr, "ring_buffer allocation failure\n");		return 1;
	}

	r_buffer->write_head = 0;
	r_buffer->read_head = 0;
	r_buffer->size = size;
	r_buffer->mask = size - 1;

	return 0;
}

void r_buffer_free(ring_buffer* buffer) {
	
	free(buffer->r_buffer);
	free(buffer);
}

float r_buffer_read(ring_buffer* buffer) {

	int read = buffer->read_head;
	float sample = buffer->r_buffer[read];

	read++;
	read = read & buffer->mask;
	buffer->read_head = read;

	return sample;
}

float r_buffer_read_index(ring_buffer* buffer, unsigned int index) {

	float sample;
	index = index & buffer->mask;
	sample = buffer->r_buffer[index];

	return sample;
}

void r_buffer_read_block(ring_buffer* buffer, int num_samples, float* out_buf) {

	for (int i = 0; i < num_samples; i++) {

		out_buf[i] = r_buffer_read(buffer);
	}
}

void r_buffer_read_delayed_bloc(ring_buffer* buffer, int num_samples, float* out_buf, int delay_in_samples) {

	//unsigned int read_index = wrap_index(buffer->write_head, (-1 * delay_in_samples), buffer->mask);			 r_buffer_read_index already handles the wrap
	unsigned int read_index = buffer->write_head - delay_in_samples;

	for (int i = 0; i < num_samples; i++) {
	
		out_buf[i] = r_buffer_read_index(buffer, read_index++);
	}

}

void r_buffer_write(ring_buffer* buffer, float sample) {

	int write = buffer->write_head;
	buffer->r_buffer[write] = sample;

	write++;
	write = write & buffer->mask;
	buffer->write_head = write;
}

void r_buffer_write_block(ring_buffer* buffer, int num_samples, float* input) {

	for(int i = 0; i < num_samples; i++) {

		r_buffer_write(buffer, input[i]);
	}
}

float r_buffer_write_index(ring_buffer* buffer, int index, float sample) {

	index = index & buffer->mask;

	buffer->r_buffer[index] = sample;
	return sample;
}

unsigned int wrap_index(unsigned int current_index, int index_mod, unsigned int mask) {

	unsigned int new_index = current_index + index_mod;
	new_index = new_index & mask;

	return new_index;
}

/*
*	@brief accepts an input buffer, and an output buffer scaled for the upsampling factor
*/
void upsample(float* input_buf, int buffer_size, float* upsample_buf, int upsample_factor) {
	
	int i = 0, j = 0;
	while (i < buffer_size) {

		upsample_buf[j] = input_buf[i];
		j++;

		int k = 1;
		while (k < upsample_factor) {

			upsample_buf[j] = 0;
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


biquad* alias_init(int upsample_factor, int sample_rate) {
	
	int oversampled_rate = sample_rate * upsample_factor;
	biquad* alias_filter = bq_new(LOWPASS, ALIAS_CUTOFF, BUTTERWORTH_Q, UNITY_GAIN, oversampled_rate);
	return alias_filter;
}

void alias_terminate(biquad* alias_filter) {
	bq_destroy(alias_filter);
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

float convert_to_range(float value, float min, float max) {

	float new_range;
	new_range = value * (max - min) + min;
	return new_range;
}

float rectify(float sample) {

	return fabsf(sample);
}

// A softer rectification
float soft_rectify(float sample, float knee) {

	float rect = sqrtf(sample * sample + knee * knee) - knee;
	return rect;
}