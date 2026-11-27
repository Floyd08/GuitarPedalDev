#include <stdio.h>
#include <math.h>
#include "unity.h"
#include "effects_engine.h"
#include "amp_model.h"

void setUp(void) {}				// runs before each test
void tearDown(void) {}			// runs after each test

// tone stack tests
void test_ts_all_bands();

float measure_gain_db(tone_stack* ts, float freq, int sample_rate);

int main(void) {

	UNITY_BEGIN();

	// tone stack tests
	RUN_TEST(test_ts_all_bands);

	return UNITY_END();
}

// tone stack tests
void test_ts_all_bands() {

	float b_freq = 50, m_freq = 1000, t_freq = 10000;
	float sample_rate = 48000;
	float expected = 0.0f;
	float b_actual = -1, m_actual = -1, t_actual = -1;

	tone_stack t_s;
	tone_stack_init(&t_s, sample_rate);

	op_update(t_s.bass_lsf, OP_LOWSHELF,  b_freq, 0, sample_rate);
	bq_update(t_s.mid_peak, PEAK, m_freq, 0.7071, 0, sample_rate);
	op_update(t_s.treble_hsf, OP_LOWSHELF,  t_freq, 0, sample_rate);

	b_actual = measure_gain_db(&t_s, b_freq, sample_rate);
	m_actual = measure_gain_db(&t_s, m_freq, sample_rate);
	t_actual = measure_gain_db(&t_s, t_freq, sample_rate);

	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.05f, expected, b_actual, "50Hz");
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.05f, expected, m_actual, "1000Hz");
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.05f, expected, t_actual, "10000Hz");

}


// Helper functions
float measure_gain_db(tone_stack* ts, float freq, int sample_rate) {

	float two_pi = 6.283185307f;

	// run length and the pause to let the filters settle
	const int length = (int)(0.5f * sample_rate);
    const int settle = (int)(0.1f * sample_rate);

	// running values of the input and output, squared for the RMS calc later
	double in_sq = 0.0, out_sq = 0.0;

	for (int i = 0; i < length ; i++) {

        float x = 0.5f * sinf(two_pi * freq * (float)i / sample_rate);
        float y = process_tone_stack(ts, x);

        if (i >= settle) {

            in_sq  = in_sq + (double)x * x;
            out_sq = out_sq + (double)y * y;
        }
    }

    float in_rms  = (float)sqrt(in_sq  / (length  - settle));
    float out_rms = (float)sqrt(out_sq / (length  - settle));

    return 20.0f * log10f(out_rms / in_rms);
}