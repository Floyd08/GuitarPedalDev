#include <stdio.h>
#include <math.h>
#include "dsp_tools.h"

// void test_upsampling();
// void test_downsampling();
// void test_alias_filtering();
void test_envelope_sampler();
void test_ring_buffer();

int main(void) {

	//test_upsampling();
	//test_downsampling();
	//test_alias_filtering();
	//test_envelope_sampler();
	test_ring_buffer();
}
/*
void test_upsampling() {

	int upsampling_factor = 2, input_buffer_length = 32;
	float test_input[] = { 0.18150537, -0.39924803, 0.54851033, -0.91744622, 
  		-0.51886983, 0.01373006, 0.95018129, -0.05818760, 
  		0.14840434, -0.25563693, -0.39027987, 0.59321034, 
  		-0.58573823, -0.55687726, 0.79525340, -0.44634769, 
  		-0.57492056, 0.35728502, -0.27979117, -0.22815813, 
  		-0.73574030, -0.89472514, 0.21611137, -0.08051311, 
  		-0.37155920, -0.87450661, 0.70875736, -0.33462605, 
  		-0.65752867, -0.46222064, 0.48096307, -0.45818636
	};		
	float test_output[input_buffer_length * upsampling_factor];

	// for (int i = 0; i < 32; i++) {

	// 	printf("float at index: %d -> %f\n", i, test_input[i]);
	// }

	upsample(test_input, input_buffer_length, test_output, upsampling_factor);

	for (int i = 0; i < input_buffer_length * upsampling_factor; i++) {

		printf("float at index: %d -> %f\n", i, test_output[i]);
	}
}

void test_downsampling() {

	int upsampling_factor = 2, output_buffer_length = 32;
	float test_input[] = { 0.18150537f, 0.0f, -0.39924803f, 0.0f,  0.54851033f, 0.0f, -0.91744622f, 0.0f,
   		-0.51886983f, 0.0f,  0.01373006f, 0.0f,  0.95018129f, 0.0f, -0.05818760f, 0.0f,
    	0.14840434f, 0.0f, -0.25563693f, 0.0f, -0.39027987f, 0.0f,  0.59321034f, 0.0f,
   		-0.58573823f, 0.0f, -0.55687726f, 0.0f,  0.79525340f, 0.0f, -0.44634769f, 0.0f,
   		-0.57492056f, 0.0f,  0.35728502f, 0.0f, -0.27979117f, 0.0f, -0.22815813f, 0.0f,
   		-0.73574030f, 0.0f, -0.89472514f, 0.0f,  0.21611137f, 0.0f, -0.08051311f, 0.0f,
   		-0.37155920f, 0.0f, -0.87450661f, 0.0f,  0.70875736f, 0.0f, -0.33462605f, 0.0f,
   		-0.65752867f, 0.0f, -0.46222064f, 0.0f,  0.48096307f, 0.0f, -0.45818636f, 0.0f
	}; 
	float test_output[output_buffer_length];

	downsample(test_input, upsampling_factor, test_output, output_buffer_length);

	for(int i = 0; i < output_buffer_length; i++) {

		printf("float at index: %d -> %f\n", i, test_output[i]);
	}
}

void test_alias_filtering() {

	int upsampling_factor = 2, input_buffer_length = 32, output_buffer_length = 32, sample_rate = 48000;
	float test_input[32] = { 0.6000f, -0.2616f,  1.0732f,  0.4563f,  1.1365f,  1.0000f,  0.7656f,  1.1617f,
    	0.1024f,  0.8797f, -0.6000f,  0.2616f, -1.0732f, -0.4563f, -1.1365f, -1.0000f,
   		-0.7656f, -1.1617f, -0.1024f, -0.8797f,  0.6000f, -0.2616f,  1.0732f,  0.4563f,
    	1.1365f,  1.0000f,  0.7656f,  1.1617f,  0.1024f,  0.8797f, -0.6000f,  0.2616f
	};
	float upsampled_buffer[input_buffer_length * upsampling_factor];
	float test_output[input_buffer_length];
	biquad* aliasing_filter = alias_init(upsampling_factor, sample_rate);

	upsample(test_input, input_buffer_length, upsampled_buffer, upsampling_factor);
	alias_filter_process(aliasing_filter, input_buffer_length, upsampled_buffer, upsampling_factor);
	downsample(upsampled_buffer, upsampling_factor, test_output, output_buffer_length);

	for(int i = 0; i < input_buffer_length; i++) {

		printf("float at index: %d -> %f\n", i, test_output[i]);
	}

}
*/

void test_envelope_sampler() {

	float sample_rate = 1000;
	float attack = 0.01, release = 0.05;

	float input[] = {0, 1, 1, 1, 1, 1, 1, 1,
						1, 1, 0, 0, 0, 1, 1, 1};
	int input_length = 16;

	envelope_follower e_fol;
	envelope_follower_init(&e_fol, attack, release, sample_rate);

	for (int i = 0; i < input_length; i++) {

		printf ("sample #%d: current state: %f\n", i, e_fol.state);
		process_envelope_follower(&e_fol, input[i]);
		printf ("sample #%d: new state: %f\n", i, e_fol.state);
	}
}

void test_ring_buffer() {

	printf("begin ring buffer test\n");

	float sine_440hz[16] = {
    	0.000000f, 0.062648f, 0.125051f, 0.186961f, 
    	0.248138f, 0.308339f, 0.367330f, 0.424877f, 
    	0.480755f, 0.534744f, 0.586632f, 0.636216f, 
    	0.683300f, 0.727699f, 0.769240f, 0.807759f
	};

	float correct_output[8] = {
		0.480755f, 0.597392f, 0.711683f, 0.823177f, 
    	0.931438f, 1.036038f, 1.136570f, 1.232636f
	};

	float test_output[16] = {
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0
	};

	ring_buffer r_buf;
	r_buffer_init(&r_buf, 16);
	int delay = 8;						//delay by 8 samples

	r_buffer_write_block(&r_buf, 16, sine_440hz);
	for(int i = 0; i < 16; i++) {
		printf("Test_sample %d: %f\n", i, r_buf.r_buffer[i]);
	}

	r_buf.write_head = 0;
	r_buf.read_head = r_buf.write_head + delay;

	for(int i = 0; i < delay; i++) {
		
		float sample1 = r_buffer_read(&r_buf);
		float sample2 = r_buffer_read_index(&r_buf, i);

		r_buffer_write(&r_buf, (sample1 + sample2) );
	}

	r_buf.read_head = 0;
	r_buffer_read_block(&r_buf, 16, test_output);

	for(int i = 0; i < 16; i++) {
		printf("Output_sample %d: %f\n", i, test_output[i]);
	}

}