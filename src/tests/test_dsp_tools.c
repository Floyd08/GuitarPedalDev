#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "dsp_tools.h"

static void ap_close(void);		// frees the allpass under test (defined with the allpass tests)
static void vc_close_all(void);	// frees the comb filters under test (defined with the comb tests)

void setUp(void) {}				// runs before each test
void tearDown(void) { ap_close(); vc_close_all(); }			// runs after each test

// Ring buffer tests
void test_ring_buffer();

// Linear interpolater tests
void test_linear_interp_negative();

// Allpass filter tests
void test_allpass_init_sets_fields(void);
void test_allpass_init_delay_samples_44k1(void);
void test_allpass_init_invalid_buffer_length(void);
void test_allpass_impulse_response(void);
void test_allpass_zero_gain_is_pure_delay(void);
void test_allpass_matches_difference_equation(void);
void test_allpass_unity_magnitude_response(void);
void test_allpass_impulse_energy_is_unity(void);
void test_allpass_delay_equals_buffer_length(void);
void test_allpass_delay_exceeds_buffer_length(void);
void test_allpass_update_gain_applies_new_gain(void);
void test_allpass_update_gain_preserves_state(void);

// Comb filter tests
void test_verb_comb_init_sets_fields(void);
void test_verb_comb_init_delay_samples_44k1(void);
void test_verb_comb_init_rejects_invalid_config(void);
void test_verb_comb_first_echo(void);
void test_verb_comb_zero_gain_is_pure_delay(void);
void test_verb_comb_matches_reference_model(void);
void test_verb_comb_dc_gain(void);
void test_verb_comb_damping_attenuates_highs(void);
void test_verb_comb_impulse_tail_decays(void);
void test_verb_comb_instances_are_independent(void);
void test_verb_comb_delay_equals_buffer_length(void);
void test_verb_comb_update_gain_applies_new_gain(void);
void test_verb_comb_update_gain_preserves_state(void);

int main(void) {

	UNITY_BEGIN();

	// Ring buffer tests
	RUN_TEST(test_ring_buffer);

	// Linear interpolater tests
	RUN_TEST(test_linear_interp_negative);

	// Allpass filter tests
	RUN_TEST(test_allpass_init_sets_fields);
	RUN_TEST(test_allpass_init_delay_samples_44k1);
	RUN_TEST(test_allpass_init_invalid_buffer_length);
	RUN_TEST(test_allpass_impulse_response);
	RUN_TEST(test_allpass_zero_gain_is_pure_delay);
	RUN_TEST(test_allpass_matches_difference_equation);
	RUN_TEST(test_allpass_unity_magnitude_response);
	RUN_TEST(test_allpass_impulse_energy_is_unity);
	RUN_TEST(test_allpass_delay_equals_buffer_length);
	RUN_TEST(test_allpass_delay_exceeds_buffer_length);
	RUN_TEST(test_allpass_update_gain_applies_new_gain);
	RUN_TEST(test_allpass_update_gain_preserves_state);

	// Comb filter tests
	RUN_TEST(test_verb_comb_init_sets_fields);
	RUN_TEST(test_verb_comb_init_delay_samples_44k1);
	RUN_TEST(test_verb_comb_init_rejects_invalid_config);
	RUN_TEST(test_verb_comb_first_echo);
	RUN_TEST(test_verb_comb_zero_gain_is_pure_delay);
	RUN_TEST(test_verb_comb_matches_reference_model);
	RUN_TEST(test_verb_comb_dc_gain);
	RUN_TEST(test_verb_comb_damping_attenuates_highs);
	RUN_TEST(test_verb_comb_impulse_tail_decays);
	RUN_TEST(test_verb_comb_instances_are_independent);
	RUN_TEST(test_verb_comb_delay_equals_buffer_length);
	RUN_TEST(test_verb_comb_update_gain_applies_new_gain);
	RUN_TEST(test_verb_comb_update_gain_preserves_state);

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

/*----------------------------------------------------------------------------------------------*/
// Allpass filter tests
//
// These describe a correct Schroeder allpass
//
//     H(z) = (-g + z^-D) / (1 - g*z^-D)        y[n] = -g*x[n] + x[n-D] + g*y[n-D]
//
//  - impulse response: h[0] = -g, h[k*D] = (1 - g^2) * g^(k-1) for k >= 1, zero elsewhere
//  - |H| = 1 at every frequency, so the impulse response has unit energy
//  - D = length_ms * sample_rate / 1000, rounded to the nearest sample
//  - allpass_init() returns 0 on success, and non-zero if dl_length isn't a power of two >= 2
//    or the delay doesn't fit in it (D == dl_length is fine)
//  - after a failed allpass_init(), ap_buffer.r_buffer is NULL, so allpass_free() is safe
//  - ap_update_gain() takes effect on the next sample and leaves the delay line alone

#define AP_RATE			48000	// sample rate used by most of the allpass tests
#define AP_DELAY_MS		1		// 1 ms at 48 kHz...
#define AP_D			48		// ...is 48 samples
#define AP_LINE			64		// delay line length: a power of two, longer than AP_D

// The filter under test. ap_open() fills it with junk before calling allpass_init(), so a
// test can't pass just because the memory happened to be zeroed. tearDown() frees it, so
// nothing leaks when an assertion stops a test part-way through.
static allpass_filter ap;
static int ap_needs_free = 0;

static void ap_close(void) {

	if (ap_needs_free) {
		allpass_free(&ap);
		ap_needs_free = 0;
	}
}

static int ap_open(unsigned int length_ms, unsigned int dl_length, int sample_rate, float gain) {

	ap_close();
	memset(&ap, 0xA5, sizeof ap);

	int rc = allpass_init(&ap, length_ms, dl_length, sample_rate, gain);
	ap_needs_free = (rc == 0);
	return rc;
}

static void ap_open_ok(unsigned int length_ms, unsigned int dl_length, int sample_rate, float gain) {

	int rc = ap_open(length_ms, dl_length, sample_rate, gain);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "allpass_init() failed for a valid configuration");
}

// Feeds in[0..n-1] through the filter, one sample at a time
static void run_allpass(allpass_filter* apf, const float* in, float* out, int n) {

	for (int i = 0; i < n; i++) {
		out[i] = process_allpass(apf, in[i]);
	}
}

// Feeds a unit impulse followed by n-1 zeros
static void run_impulse(allpass_filter* apf, float* out, int n) {

	for (int i = 0; i < n; i++) {
		out[i] = process_allpass(apf, (i == 0) ? 1.0f : 0.0f);
	}
}

// Closed-form impulse response: h[0] = -g, h[k*d] = (1 - g^2) * g^(k-1), zero elsewhere
static void expected_impulse(float* h, int n, int d, float g) {

	double tap = 1.0 - (double)g * g;

	for (int i = 0; i < n; i++) {
		h[i] = 0.0f;
	}
	h[0] = -g;
	for (int i = d; i < n; i += d) {
		h[i] = (float)tap;
		tap *= g;
	}
}

// The difference equation evaluated directly, in double precision
static void reference_allpass(const float* x, float* y, int n, int d, float g) {

	double* y_hist = malloc(n * sizeof *y_hist);
	TEST_ASSERT_NOT_NULL(y_hist);

	for (int i = 0; i < n; i++) {
		double x_del = (i >= d) ? x[i - d] : 0.0;
		double y_del = (i >= d) ? y_hist[i - d] : 0.0;
		y_hist[i] = -(double)g * x[i] + x_del + (double)g * y_del;
		y[i] = (float)y_hist[i];
	}
	free(y_hist);
}

// Deterministic pseudo-random samples in [-1, 1), so any failure is reproducible
static float next_noise(uint32_t* state) {

	*state = *state * 1664525u + 1013904223u;
	return (float)((double)(*state >> 8) / 8388608.0 - 1.0);
}

static double rms(const float* x, int n) {

	double sum = 0.0;
	for (int i = 0; i < n; i++) {
		sum += (double)x[i] * x[i];
	}
	return sqrt(sum / n);
}


// allpass_init

void test_allpass_init_sets_fields(void) {

	int rc = ap_open(10, 512, 48000, 0.5f);

	TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "allpass_init() should succeed");
	TEST_ASSERT_EQUAL_FLOAT(10.0f, ap.ap_delay_ms);
	TEST_ASSERT_EQUAL_UINT(480u, ap.ap_delay_samples);		// 10 ms * 48 samples/ms
	TEST_ASSERT_EQUAL_FLOAT(0.5f, ap.ap_gain);
	TEST_ASSERT_NOT_NULL(ap.ap_buffer.r_buffer);
	TEST_ASSERT_EQUAL_UINT(512u, ap.ap_buffer.size);
	TEST_ASSERT_EQUAL_UINT(511u, ap.ap_buffer.mask);
	TEST_ASSERT_EQUAL_INT(0, ap.ap_buffer.write_head);
}

void test_allpass_init_delay_samples_44k1(void) {

	// 10 ms at 44.1 kHz is exactly 441 samples
	ap_open_ok(10, 512, 44100, 0.5f);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(441u, ap.ap_delay_samples, "10 ms at 44.1 kHz");

	// 7 ms at 44.1 kHz is 308.7 samples, which should round up to 309 rather than truncate
	ap_open_ok(7, 512, 44100, 0.5f);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(309u, ap.ap_delay_samples, "7 ms at 44.1 kHz");
}

void test_allpass_init_invalid_buffer_length(void) {

	const unsigned int bad_lengths[] = { 1000u, 1u };	// not a power of two; shorter than 2
	char msg[96];

	for (int i = 0; i < 2; i++) {
		int rc = ap_open(AP_DELAY_MS, bad_lengths[i], AP_RATE, 0.5f);

		snprintf(msg, sizeof msg, "dl_length = %u: allpass_init() should return an error", bad_lengths[i]);
		TEST_ASSERT_NOT_EQUAL_MESSAGE(0, rc, msg);
		snprintf(msg, sizeof msg, "dl_length = %u: a failed init should leave r_buffer NULL", bad_lengths[i]);
		TEST_ASSERT_NULL_MESSAGE(ap.ap_buffer.r_buffer, msg);

		allpass_free(&ap);		// has to be safe after a failed init
	}
}


// process_allpass

void test_allpass_impulse_response(void) {

	enum { N = 5 * AP_D + 1 };					// five echoes
	float expected[N], actual[N];
	const float g = 0.5f;

	ap_open_ok(AP_DELAY_MS, AP_LINE, AP_RATE, g);
	run_impulse(&ap, actual, N);
	expected_impulse(expected, N, AP_D, g);		// -0.5, then 0.75, 0.375, 0.1875... every 48 samples

	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-6f, expected, actual, N);
}

void test_allpass_zero_gain_is_pure_delay(void) {

	enum { N = 8 * AP_LINE };					// eight trips around the delay line
	float in[N], expected[N], actual[N];

	for (int i = 0; i < N; i++) {
		in[i] = (float)(i + 1) / N;				// ramp: every sample distinct and non-zero
		expected[i] = (i >= AP_D) ? in[i - AP_D] : 0.0f;
	}

	ap_open_ok(AP_DELAY_MS, AP_LINE, AP_RATE, 0.0f);
	run_allpass(&ap, in, actual, N);

	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-7f, expected, actual, N);
}

void test_allpass_matches_difference_equation(void) {

	enum { N = 20 * AP_LINE };					// twenty trips around the delay line
	static float in[N], expected[N], actual[N];
	const float gains[] = { 0.5f, 0.9f, -0.6f };
	char msg[32];

	for (int k = 0; k < 3; k++) {
		uint32_t seed = 12345u;
		for (int i = 0; i < N; i++) {
			in[i] = next_noise(&seed);
		}

		ap_open_ok(AP_DELAY_MS, AP_LINE, AP_RATE, gains[k]);
		run_allpass(&ap, in, actual, N);
		reference_allpass(in, expected, N, AP_D, gains[k]);

		snprintf(msg, sizeof msg, "gain = %.1f", gains[k]);
		TEST_ASSERT_FLOAT_ARRAY_WITHIN_MESSAGE(1e-5f, expected, actual, N, msg);
	}
}

void test_allpass_unity_magnitude_response(void) {

	enum { N = 100 * AP_D, SETTLE = 80 * AP_D };	// 100 ms, skipping the first 80 ms
	// period in samples: 0 = DC, 2 = Nyquist, the rest are sines from 480 Hz to 6.9 kHz
	const int periods[] = { 0, 2, 100, 64, 20, 7 };
	static float in[N], out[N];
	const float g = 0.7f;
	char msg[32];

	for (int k = 0; k < 6; k++) {
		const int p = periods[k];
		for (int i = 0; i < N; i++) {
			if (p == 0) {
				in[i] = 1.0f;
			}
			else if (p == 2) {
				in[i] = (i & 1) ? -1.0f : 1.0f;
			}
			else {
				in[i] = sinf(TWO_PI * (float)(i % p) / (float)p);
			}
		}

		ap_open_ok(AP_DELAY_MS, AP_LINE, AP_RATE, g);
		run_allpass(&ap, in, out, N);

		// compare steady-state levels over a whole number of periods
		int len = N - SETTLE;
		if (p > 2) {
			len -= len % p;
		}
		double ratio = rms(out + N - len, len) / rms(in + N - len, len);

		snprintf(msg, sizeof msg, "period = %d samples", p);
		TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, 1.0f, (float)ratio, msg);
	}
}

void test_allpass_impulse_energy_is_unity(void) {

	enum { N = 200 * AP_D };					// 200 echoes
	static float h[N];
	const float g = 0.9f;
	double energy = 0.0;
	float tail = 0.0f;

	ap_open_ok(AP_DELAY_MS, AP_LINE, AP_RATE, g);
	run_impulse(&ap, h, N);

	for (int i = 0; i < N; i++) {
		energy += (double)h[i] * h[i];
	}
	for (int i = N - AP_D; i < N; i++) {
		tail = fmaxf(tail, fabsf(h[i]));
	}

	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, 1.0f, (float)energy, "sum of h^2 should be 1");
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-6f, 0.0f, tail, "impulse response should have died away");
}


// delay vs. ring buffer length

void test_allpass_delay_equals_buffer_length(void) {

	// 2 ms at 32 kHz is 64 samples, which exactly fills a 64-sample line
	enum { D = 64, N = 3 * D + 1 };
	float expected[N], actual[N];
	const float g = 0.5f;

	ap_open_ok(2, D, 32000, g);
	run_impulse(&ap, actual, N);
	expected_impulse(expected, N, D, g);		// first echo exactly at n = 64

	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-6f, expected, actual, N);
}

void test_allpass_delay_exceeds_buffer_length(void) {

	const unsigned int delay_ms[] = { 1, 10 };		// 48 and 480 samples at 48 kHz...
	const unsigned int line_len[] = { 32, 256 };	// ...into lines that are too short
	char msg[96];

	for (int i = 0; i < 2; i++) {
		int rc = ap_open(delay_ms[i], line_len[i], AP_RATE, 0.5f);

		snprintf(msg, sizeof msg, "%u ms into a %u-sample line: allpass_init() should return an error",
				 delay_ms[i], line_len[i]);
		TEST_ASSERT_NOT_EQUAL_MESSAGE(0, rc, msg);
		snprintf(msg, sizeof msg, "%u ms into a %u-sample line: a failed init should leave r_buffer NULL",
				 delay_ms[i], line_len[i]);
		TEST_ASSERT_NULL_MESSAGE(ap.ap_buffer.r_buffer, msg);

		allpass_free(&ap);		// has to be safe after a failed init
	}
}


// ap_update_gain

void test_allpass_update_gain_applies_new_gain(void) {

	enum { N = 3 * AP_D + 1 };
	float expected[N], actual[N];

	ap_open_ok(AP_DELAY_MS, AP_LINE, AP_RATE, 0.5f);
	ap_update_gain(&ap, 0.7f);

	run_impulse(&ap, actual, N);
	expected_impulse(expected, N, AP_D, 0.7f);	// -0.7, then 0.51, 0.357...

	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-6f, expected, actual, N);
}

void test_allpass_update_gain_preserves_state(void) {

	enum { N = 4 * AP_D + 1 };
	float y[N], echoes_only[N];
	const float g_old = 0.5f, g_new = 0.25f;

	ap_open_ok(AP_DELAY_MS, AP_LINE, AP_RATE, g_old);
	for (int i = 0; i < N; i++) {
		if (i == AP_D) {
			ap_update_gain(&ap, g_new);			// the impulse's first echo is still in the line
		}
		y[i] = process_allpass(&ap, (i == 0) ? 1.0f : 0.0f);
	}

	// before the change, the direct path uses the old gain
	TEST_ASSERT_FLOAT_WITHIN(1e-6f, -g_old, y[0]);

	// The first echo must survive the gain change. Its exact level depends on the allpass
	// structure, but for the standard ones it lies between 1 - g_old^2 and 1 - g_new^2.
	const float lo = 1.0f - g_old * g_old, hi = 1.0f - g_new * g_new;
	const float mid = (hi + lo) / 2, half_width = (hi - lo) / 2 + 1e-6f;
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(half_width, mid, y[AP_D], "first echo after the gain change");

	// from then on the new gain sets the decay: each echo is g_new times the one before
	for (int k = 2; k <= 4; k++) {
		float prev = y[(k - 1) * AP_D];
		TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-6f, g_new * prev, y[k * AP_D], "echo should be g_new times the previous one");
	}

	// and nothing appears between the echoes
	for (int i = 0; i < N; i++) {
		echoes_only[i] = (i % AP_D == 0) ? y[i] : 0.0f;
	}
	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-6f, echoes_only, y, N);
}


/*----------------------------------------------------------------------------------------------*/
// Comb filter tests
//
// These describe a correct lowpass-feedback comb (the reverb comb from Schroeder and Moorer, as
// used in Freeverb):
//
//     y[n] = v[n-D]                  the output is the delay line's output (no direct path)
//     v[n] = x[n] + g * LPF{y}[n]    the line takes the input plus damped, scaled feedback
//
//  - the first echo is the input itself, exactly D samples later
//  - the damping lowpass has unity gain at DC, so a constant input settles at 1 / (1 - g)
//    whatever the damping; damping only makes the high frequencies die away faster
//  - with |g| < 1 the tail decays at least as fast as g per trip round the loop
//  - D = length_ms * sample_rate / 1000, rounded to the nearest sample
//  - verb_comb_init() returns 0 on success, and non-zero if dl_length isn't a power of two >= 2
//    or the delay doesn't fit in it (D == dl_length is fine)
//  - after a failed verb_comb_init(), r_buffer and lpf are both NULL, so verb_comb_free() is safe
//  - verb_comb_update_gain() takes effect on the next sample and leaves the delay line alone
//  - separate combs share no state

#define VC_RATE			48000		// sample rate used by most of the comb tests
#define VC_DELAY_MS		1			// 1 ms at 48 kHz...
#define VC_D			48			// ...is 48 samples, so the comb resonates every 1 kHz
#define VC_LINE			64			// delay line length: a power of two, longer than VC_D
#define VC_DAMP_LOW		1500.0f		// damping cutoffs: heavy,
#define VC_DAMP_MID		10000.0f	// the default,
#define VC_DAMP_HIGH	15000.0f	// and light

// The combs under test (two slots, for the independence test). vc_open() fills a slot with junk
// before calling verb_comb_init(), and tearDown() frees whatever is still open.
static verb_comb_filter vc[2];
static int vc_needs_free[2] = { 0, 0 };

static void vc_close(int slot) {

	if (vc_needs_free[slot]) {
		verb_comb_free(&vc[slot]);
		vc_needs_free[slot] = 0;
	}
}

static void vc_close_all(void) {

	vc_close(0);
	vc_close(1);
}

static int vc_open(int slot, unsigned int length_ms, unsigned int dl_length, int sample_rate,
				   float lpf_hz, float gain) {

	vc_close(slot);
	memset(&vc[slot], 0xA5, sizeof vc[slot]);

	int rc = verb_comb_init(&vc[slot], length_ms, dl_length, sample_rate, lpf_hz, gain);
	vc_needs_free[slot] = (rc == 0);
	return rc;
}

static void vc_open_ok(int slot, unsigned int length_ms, unsigned int dl_length, int sample_rate,
					   float lpf_hz, float gain) {

	int rc = vc_open(slot, length_ms, dl_length, sample_rate, lpf_hz, gain);
	TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "verb_comb_init() failed for a valid configuration");
}

// Feeds in[0..n-1] through the comb, one sample at a time
static void run_comb(verb_comb_filter* vcf, const float* in, float* out, int n) {

	for (int i = 0; i < n; i++) {
		out[i] = process_verb_comb(vcf, in[i]);
	}
}

// Feeds a unit impulse followed by n-1 zeros
static void run_comb_impulse(verb_comb_filter* vcf, float* out, int n) {

	for (int i = 0; i < n; i++) {
		out[i] = process_verb_comb(vcf, (i == 0) ? 1.0f : 0.0f);
	}
}

// Feeds a constant input for n samples and returns the last output
static float settled_output(verb_comb_filter* vcf, float input, int n) {

	float y = 0.0f;
	for (int i = 0; i < n; i++) {
		y = process_verb_comb(vcf, input);
	}
	return y;
}

// The comb evaluated straight from its definition, y[n] = v[n-D] and v[n] = x[n] + g * LPF{y}[n],
// with its own one-pole built from the same settings as the comb's
static void reference_comb(const float* x, float* y, int n, int d, float g, float lpf_hz, int sample_rate) {

	float* v = malloc(n * sizeof *v);
	onepole* lpf = op_new(OP_LOWPASS, lpf_hz, 0.0f, sample_rate);

	if (v == NULL || lpf == NULL) {
		free(v);
		op_destroy(lpf);
		TEST_FAIL_MESSAGE("reference_comb() setup failed");
		return;
	}
	for (int i = 0; i < n; i++) {
		y[i] = (i >= d) ? v[i - d] : 0.0f;
		v[i] = x[i] + g * op_process(lpf, y[i]);
	}
	free(v);
	op_destroy(lpf);
}

// Steady-state output/input RMS for a sine with the given period (in samples), through a fresh
// comb in slot 0
static double comb_sine_gain(float lpf_hz, float g, int period) {

	enum { N = 100 * VC_D, SETTLE = 80 * VC_D };
	static float in[N], out[N];

	for (int i = 0; i < N; i++) {
		in[i] = sinf(TWO_PI * (float)(i % period) / (float)period);
	}
	vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, lpf_hz, g);
	run_comb(&vc[0], in, out, N);

	int len = N - SETTLE;
	len -= len % period;							// a whole number of periods
	return rms(out + N - len, len) / rms(in + N - len, len);
}

// The same measurement through the damping lowpass on its own
static double onepole_sine_gain(float lpf_hz, int period) {

	enum { N = 20 * VC_D };
	float in[N], out[N];
	onepole* lpf = op_new(OP_LOWPASS, lpf_hz, 0.0f, VC_RATE);
	TEST_ASSERT_NOT_NULL(lpf);

	for (int i = 0; i < N; i++) {
		in[i] = sinf(TWO_PI * (float)(i % period) / (float)period);
		out[i] = op_process(lpf, in[i]);
	}
	op_destroy(lpf);

	int len = N / 2;
	len -= len % period;
	return rms(out + N - len, len) / rms(in + N - len, len);
}


// verb_comb_init

void test_verb_comb_init_sets_fields(void) {

	int rc = vc_open(0, 10, 512, 48000, VC_DAMP_MID, 0.84f);

	TEST_ASSERT_EQUAL_INT_MESSAGE(0, rc, "verb_comb_init() should succeed");
	TEST_ASSERT_EQUAL_FLOAT(10.0f, vc[0].vc_delay_ms);
	TEST_ASSERT_EQUAL_UINT(480u, vc[0].vc_delay_samples);		// 10 ms * 48 samples/ms
	TEST_ASSERT_EQUAL_FLOAT(0.84f, vc[0].comb_gain);
	TEST_ASSERT_NOT_NULL(vc[0].vc_buffer.r_buffer);
	TEST_ASSERT_EQUAL_UINT(512u, vc[0].vc_buffer.size);
	TEST_ASSERT_EQUAL_UINT(511u, vc[0].vc_buffer.mask);
	TEST_ASSERT_EQUAL_INT(0, vc[0].vc_buffer.write_head);
	TEST_ASSERT_NOT_NULL(vc[0].lpf);
}

void test_verb_comb_init_delay_samples_44k1(void) {

	// 10 ms at 44.1 kHz is exactly 441 samples
	vc_open_ok(0, 10, 512, 44100, VC_DAMP_MID, 0.84f);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(441u, vc[0].vc_delay_samples, "10 ms at 44.1 kHz");

	// 7 ms at 44.1 kHz is 308.7 samples, which should round up to 309 rather than truncate
	vc_open_ok(0, 7, 512, 44100, VC_DAMP_MID, 0.84f);
	TEST_ASSERT_EQUAL_UINT_MESSAGE(309u, vc[0].vc_delay_samples, "7 ms at 44.1 kHz");
}

void test_verb_comb_init_rejects_invalid_config(void) {

	// not a power of two; shorter than 2; too short for 48 samples; too short for 480 samples
	const unsigned int delay_ms[] = { 1, 1, 1, 10 };
	const unsigned int line_len[] = { 1000, 1, 32, 256 };
	char msg[96];

	for (int i = 0; i < 4; i++) {
		int rc = vc_open(0, delay_ms[i], line_len[i], VC_RATE, VC_DAMP_MID, 0.84f);

		snprintf(msg, sizeof msg, "%u ms in a %u-sample line: verb_comb_init() should return an error",
				 delay_ms[i], line_len[i]);
		TEST_ASSERT_NOT_EQUAL_MESSAGE(0, rc, msg);
		snprintf(msg, sizeof msg, "%u ms in a %u-sample line: a failed init should leave r_buffer NULL",
				 delay_ms[i], line_len[i]);
		TEST_ASSERT_NULL_MESSAGE(vc[0].vc_buffer.r_buffer, msg);
		snprintf(msg, sizeof msg, "%u ms in a %u-sample line: a failed init should leave lpf NULL",
				 delay_ms[i], line_len[i]);
		TEST_ASSERT_NULL_MESSAGE(vc[0].lpf, msg);

		verb_comb_free(&vc[0]);		// has to be safe after a failed init
	}
}


// process_verb_comb

void test_verb_comb_first_echo(void) {

	enum { N = 2 * VC_D + 1 };
	float expected[N], actual[N];

	vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, VC_DAMP_MID, 0.84f);
	run_comb_impulse(&vc[0], actual, N);

	// silent, then the impulse itself at n = D, then silent until the second echo at 2D
	for (int i = 0; i < N; i++) {
		expected[i] = 0.0f;
	}
	expected[VC_D] = 1.0f;

	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-7f, expected, actual, 2 * VC_D);
	TEST_ASSERT_TRUE_MESSAGE(actual[2 * VC_D] > 0.0f, "the second echo should start at n = 2D");
}

void test_verb_comb_zero_gain_is_pure_delay(void) {

	enum { N = 8 * VC_LINE };					// eight trips around the delay line
	float in[N], expected[N], actual[N];

	for (int i = 0; i < N; i++) {
		in[i] = (float)(i + 1) / N;				// ramp: every sample distinct and non-zero
		expected[i] = (i >= VC_D) ? in[i - VC_D] : 0.0f;
	}

	vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, VC_DAMP_MID, 0.0f);
	run_comb(&vc[0], in, actual, N);

	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-7f, expected, actual, N);
}

void test_verb_comb_matches_reference_model(void) {

	enum { N = 20 * VC_LINE };					// twenty trips around the delay line
	static float in[N], expected[N], actual[N];
	const float gains[] = { 0.5f, 0.84f, -0.6f };
	const float damping[] = { VC_DAMP_LOW, VC_DAMP_MID, VC_DAMP_HIGH };
	char msg[48];

	uint32_t seed = 12345u;
	for (int i = 0; i < N; i++) {
		in[i] = next_noise(&seed);
	}

	for (int k = 0; k < 3; k++) {
		for (int j = 0; j < 3; j++) {
			vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, damping[j], gains[k]);
			run_comb(&vc[0], in, actual, N);
			reference_comb(in, expected, N, VC_D, gains[k], damping[j], VC_RATE);

			snprintf(msg, sizeof msg, "g = %.2f, damping at %.0f Hz", gains[k], damping[j]);
			TEST_ASSERT_FLOAT_ARRAY_WITHIN_MESSAGE(1e-5f, expected, actual, N, msg);
		}
	}
}

void test_verb_comb_dc_gain(void) {

	const float gains[] = { 0.5f, 0.9f };
	const float damping[] = { VC_DAMP_LOW, VC_DAMP_HIGH };
	char msg[48];

	for (int k = 0; k < 2; k++) {
		for (int j = 0; j < 2; j++) {
			const float expected = 1.0f / (1.0f - gains[k]);	// 2 and 10, whatever the damping

			vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, damping[j], gains[k]);
			float settled = settled_output(&vc[0], 1.0f, 200 * VC_D);

			snprintf(msg, sizeof msg, "g = %.1f, damping at %.0f Hz", gains[k], damping[j]);
			TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f * expected, expected, settled, msg);
		}
	}
}

void test_verb_comb_damping_attenuates_highs(void) {

	// With D = 48 at 48 kHz the comb resonates every 1 kHz, and a 4-sample period is the 12 kHz
	// resonance. Undamped, it would peak at the DC gain, 1 / (1 - g). The damping filter passes
	// only |L| of a 12 kHz signal on each trip, so the peak can't exceed 1 / (1 - g * |L|).
	const float g = 0.84f;
	const float damping[] = { VC_DAMP_HIGH, VC_DAMP_LOW };		// light, then heavy
	double peak[2];
	char msg[112];

	for (int j = 0; j < 2; j++) {
		double lpf_gain = onepole_sine_gain(damping[j], 4);		// |L| at 12 kHz, on its own
		double limit = 1.0 / (1.0 - g * lpf_gain);
		peak[j] = comb_sine_gain(damping[j], g, 4);

		snprintf(msg, sizeof msg, "damping at %.0f Hz: 12 kHz resonance peaks at %.3f, limit %.3f (undamped %.3f)",
				 damping[j], peak[j], limit, 1.0 / (1.0 - g));
		TEST_ASSERT_TRUE_MESSAGE(peak[j] <= limit + 1e-4, msg);
	}

	snprintf(msg, sizeof msg, "12 kHz resonance: 1.5 kHz damping gave %.3f, 15 kHz gave %.3f", peak[1], peak[0]);
	TEST_ASSERT_TRUE_MESSAGE(peak[1] < peak[0], msg);
}

void test_verb_comb_impulse_tail_decays(void) {

	// g = 0.95 with a 1 ms loop: undamped, the tail would take about 0.13 s to fall 60 dB
	enum { TRIPS = 300, N = TRIPS * VC_D };			// 0.3 s
	static float h[N];
	const float g = 0.95f;
	float envelope = 1.0f;							// g^(k-1) for the k-th echo
	float peak = 0.0f;
	char msg[96];

	vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, VC_DAMP_MID, g);
	run_comb_impulse(&vc[0], h, N);

	// each trip round the loop comes back no louder than g times the one before
	for (int k = 1; k < TRIPS; k++) {
		peak = 0.0f;
		for (int i = k * VC_D; i < (k + 1) * VC_D; i++) {
			peak = fmaxf(peak, fabsf(h[i]));
		}
		if (peak > envelope + 1e-6f) {
			snprintf(msg, sizeof msg, "echo %d peaks at %g, above the g^%d envelope of %g", k, peak, k - 1, envelope);
			TEST_FAIL_MESSAGE(msg);
		}
		envelope *= g;
	}

	// after 0.3 s the last echo has died away to below -120 dB
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-6f, 0.0f, peak, "the tail should be below -120 dB after 0.3 s");
}

void test_verb_comb_instances_are_independent(void) {

	enum { N = 20 * VC_LINE };
	static float in[N], busy[N], quiet[N], solo[N], silence[N];
	uint32_t seed = 777u;

	for (int i = 0; i < N; i++) {
		in[i] = next_noise(&seed);
	}

	// two combs with different settings, processed side by side: one fed noise, one silence
	vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, VC_DAMP_LOW, 0.84f);
	vc_open_ok(1, 2, 2 * VC_LINE, VC_RATE, VC_DAMP_MID, 0.7f);
	for (int i = 0; i < N; i++) {
		busy[i] = process_verb_comb(&vc[0], in[i]);
		quiet[i] = process_verb_comb(&vc[1], 0.0f);
	}

	// the busy comb again, on its own
	vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, VC_DAMP_LOW, 0.84f);
	run_comb(&vc[0], in, solo, N);

	TEST_ASSERT_FLOAT_ARRAY_WITHIN_MESSAGE(1e-7f, silence, quiet, N, "the silent comb picked up signal");
	TEST_ASSERT_FLOAT_ARRAY_WITHIN_MESSAGE(1e-7f, solo, busy, N, "running alongside another comb changed the output");
}


// delay vs. ring buffer length

void test_verb_comb_delay_equals_buffer_length(void) {

	// 2 ms at 32 kHz is 64 samples, which exactly fills a 64-sample line
	enum { D = 64, N = 2 * D + 1 };
	float expected[N], actual[N];

	vc_open_ok(0, 2, D, 32000, VC_DAMP_MID, 0.84f);
	run_comb_impulse(&vc[0], actual, N);

	for (int i = 0; i < N; i++) {
		expected[i] = 0.0f;
	}
	expected[D] = 1.0f;								// first echo exactly at n = 64

	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-7f, expected, actual, 2 * D);
	TEST_ASSERT_TRUE_MESSAGE(actual[2 * D] > 0.0f, "the second echo should start at n = 128");
}


// verb_comb_update_gain

void test_verb_comb_update_gain_applies_new_gain(void) {

	vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, VC_DAMP_MID, 0.5f);
	verb_comb_update_gain(&vc[0], 0.8f);

	float settled = settled_output(&vc[0], 1.0f, 200 * VC_D);
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-3f, 5.0f, settled, "constant input should settle at 1 / (1 - 0.8)");
}

void test_verb_comb_update_gain_preserves_state(void) {

	float expected[VC_D], actual[VC_D];

	vc_open_ok(0, VC_DELAY_MS, VC_LINE, VC_RATE, VC_DAMP_MID, 0.5f);
	float before = settled_output(&vc[0], 1.0f, 200 * VC_D);
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-4f, 2.0f, before, "should settle at 1 / (1 - 0.5) before the change");

	verb_comb_update_gain(&vc[0], 0.8f);

	// the next D samples were already in the delay line, so they come out unchanged
	for (int i = 0; i < VC_D; i++) {
		expected[i] = before;
		actual[i] = process_verb_comb(&vc[0], 1.0f);
	}
	TEST_ASSERT_FLOAT_ARRAY_WITHIN(1e-5f, expected, actual, VC_D);

	// after that, the output settles at the new 1 / (1 - g)
	float after = settled_output(&vc[0], 1.0f, 200 * VC_D);
	TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1e-3f, 5.0f, after, "should settle at 1 / (1 - 0.8) after the change");
}