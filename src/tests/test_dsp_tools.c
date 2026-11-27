#include <stdio.h>
#include <math.h>
#include "dsp_tools.h"

void setUp(void) {}				// runs before each test
void tearDown(void) {}			// runs after each test

int test_ring_buffer();

int main(void) {

	UNITY_BEGIN();
	RUN_TEST(test_ring_buffer);

	return UNITY_END();
}

int test_ring_buffer() {

	int fail_count = 0;

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

	r_buf.write_head = 0;
	r_buf.read_head = r_buf.write_head + delay;

	for(int i = 0; i < delay; i++) {
		
		float sample1 = r_buffer_read(&r_buf);
		float sample2 = r_buffer_read_index(&r_buf, i);

		r_buffer_write(&r_buf, (sample1 + sample2) );
	}


	r_buf.read_head = 0;
	r_buffer_read_block(&r_buf, 16, test_output);

	for(int i = 0; i < 8; i++) {

		if ( !(fabsf(test_output[i] - correct_output[i]) < 1e-6f) ) {
			TEST_ASSERT_FLOAT_WITHIN(1e-6f, test_output[i], correct_output[i]);
			// fail_count = fail_count + 1;
			fprintf(stderr, "Output_sample %d: %f --- Expected sample %d: %f\n", i, test_output[i], i, correct_output[i]);
		}
	}
	return fail_count;
}