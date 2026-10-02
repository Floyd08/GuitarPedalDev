#include <stdio.h>
#include "dsp_tools.h"

void test_upsampling();
void test_downsampling();
void test_alias_filtering();

int main(void) {

	//test_upsampling();
	//test_downsampling();
	test_alias_filtering();
}

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