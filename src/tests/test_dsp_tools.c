#include <stdio.h>
#include <math.h>
#include "unity.h"
#include "dsp_tools.h"

void setUp(void) {}				// runs before each test
void tearDown(void) {}			// runs after each test

// Ring buffer tests
void test_ring_buffer();

// Linear interpolater tests
void test_linear_interp_negative();

int main(void) {

	UNITY_BEGIN();

	// Ring buffer tests
	RUN_TEST(test_ring_buffer);

	// Linear interpolater tests
	RUN_TEST(test_linear_interp_negative);

	return UNITY_END();
}

// Ring buffer tests

void test_ring_buffer() {

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

	r_buf.write_head = 0;
	r_buf.read_head = r_buf.write_head + delay;

	for(int i = 0; i < delay; i++) {
		
		float sample1 = r_buffer_read(&r_buf);
		float sample2 = r_buffer_read_index(&r_buf, i);

		r_buffer_write(&r_buf, (sample1 + sample2) );
	}


	r_buf.read_head = 0;
	r_buffer_read_block(&r_buf, 16, test_output);

	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-6f, correct_output, test_output, delay);
}


// Linear interpolater tests
void test_linear_interp_negative() {

	float sample1 = 0.6;
	float sample2 = 0.8;
	float fractional_index = -1;
	float expected = 0.6, actual = -1;

	actual = linear_interpolate(fractional_index, sample1, sample2);
	TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected, actual);

}