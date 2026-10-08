#include <stdio.h>
#include "Biquad.h"
#include "distortion_effects.h"

#define	BUTTERWORTH_Q				0.7071
#define UNITY_GAIN					0.0f
#define ALIAS_CUTOFF				20000

/*
*		Global variables go here, because I can't instantiate things at will
*/
//biquad* alias_filter; // = bq_new(LOWPASS, 20000, 0.7071, 48000);

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