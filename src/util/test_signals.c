#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdatomic.h>
#include "test_signals.h"

#define TEST_SIG_FADE_MS        5.0f            // How long the test signal takes to fade in and out
#define TEST_SIG_TWO_PI         6.283185307179586
#define TEST_SIG_PINK_SCALE     0.12f           // The pink noise filter's output peaks at about 8, measured over two minutes of
                                                // noise. This brings the peaks back to about 1.0, to match the other signals

// The kinds of test signal
enum {
	TEST_SIG_OFF,
	TEST_SIG_SINE,
	TEST_SIG_WHITE,                             // white noise: the same energy at every frequency
	TEST_SIG_PINK,                              // pink noise: the same energy in every octave, which is closer to music
	TEST_SIG_SWEEP                              // a sine that glides from TEST_SIG_SWEEP_START up to TEST_SIG_SWEEP_END, then starts again
};

#define TEST_SIG_SWEEP_START    20.0            // Hz. Where the sweep starts
#define TEST_SIG_SWEEP_END      20000.0         // Hz. Where it ends (or 0.45 x the sample rate, if that is lower)
#define TEST_SIG_SWEEP_DEFAULT_S 10.0f          // How long one sweep takes, if the command doesn't say

// The settings the generator starts with, and goes back to whenever test_signal_init() is called
#define TEST_SIG_DEFAULT_FREQ   1000.0f         // Hz
#define TEST_SIG_DEFAULT_GAIN   0.1f            // linear (-20 dB)
#define TEST_SIG_NOISE_SEED     0x2545F491u     // the noise generator's starting state. Must never be 0

static _Atomic int g_gen_sample_rate = 0;       // set by test_signal_init(). Atomic, so it can be called while audio is running
static _Atomic int g_gen_reset = 0;             // set by test_signal_init(). The callback clears its own state when it sees it

//The test signal's settings. Written by the command thread (from the panel's commands), read by the callback thread
static _Atomic int g_gen_type = TEST_SIG_OFF;
static _Atomic float g_gen_freq = TEST_SIG_DEFAULT_FREQ;     // Hz. Only the sine uses it
static _Atomic float g_gen_gain = TEST_SIG_DEFAULT_GAIN;     // linear. The peak of the sine, and (near enough) of the noise
static _Atomic float g_gen_sweep_s = TEST_SIG_SWEEP_DEFAULT_S;    // seconds. How long one sweep takes

//The test signal's state. Only the callback thread touches these, so they need no atomics
static int g_gen_playing = TEST_SIG_OFF;        // what is sounding now. It outlives g_gen_type by the length of the fade out
static float g_gen_level = 0.0f;                // the gain actually applied, which glides towards g_gen_gain (or 0)
static double g_gen_phase = 0.0;                // where the sine is in its cycle, 0.0 to 1.0
static uint32_t g_gen_random = TEST_SIG_NOISE_SEED;    // the noise generator's state. Must never be 0
static float g_gen_pink[3];                     // the pink noise filter's state
static double g_gen_sweep_freq = TEST_SIG_SWEEP_START;    // the sweep's frequency right now, in Hz

// Puts everything back as it was when the program started. The settings are reset here; the
// generator's running state belongs to the audio callback, so this only asks for that to be
// reset, and the callback does it at the start of its next block
void test_signal_init(int sample_rate) {

	atomic_store_explicit(&g_gen_type, TEST_SIG_OFF, memory_order_relaxed);   // the test signal always starts off
	atomic_store_explicit(&g_gen_freq, TEST_SIG_DEFAULT_FREQ, memory_order_relaxed);
	atomic_store_explicit(&g_gen_gain, TEST_SIG_DEFAULT_GAIN, memory_order_relaxed);
	atomic_store_explicit(&g_gen_sweep_s, TEST_SIG_SWEEP_DEFAULT_S, memory_order_relaxed);
	atomic_store_explicit(&g_gen_sample_rate, sample_rate, memory_order_relaxed);

	//Raised last, with release, so the callback sees all of the above by the time it acts on it
	atomic_store_explicit(&g_gen_reset, 1, memory_order_release);
}

// Clears the generator's running state. Only ever called from the audio callback, which owns it
static void test_signal_clear_state(void) {

	g_gen_playing = TEST_SIG_OFF;
	g_gen_level = 0.0f;
	g_gen_phase = 0.0;
	g_gen_random = TEST_SIG_NOISE_SEED;
	g_gen_pink[0] = g_gen_pink[1] = g_gen_pink[2] = 0.0f;
	g_gen_sweep_freq = TEST_SIG_SWEEP_START;
}

// The test signal. Runs in the audio callback, in place
int test_signal_process(float *samples, int count) {

	if (samples == NULL || count <= 0) {
		return 0;
	}

	//A reset asked for by test_signal_init(). Taking the flag and clearing it is one step, so a
	//reset can't be missed, or done twice
	if (atomic_exchange_explicit(&g_gen_reset, 0, memory_order_acquire)) {
		test_signal_clear_state();
	}

	int sample_rate = atomic_load_explicit(&g_gen_sample_rate, memory_order_relaxed);
	if (sample_rate <= 0) {
		return 0;                               // no sample rate yet means nothing to generate
	}

	int type = atomic_load_explicit(&g_gen_type, memory_order_relaxed);

	//Where the gain is heading. Switching off doesn't stop the signal dead: the target drops
	//to 0 and the signal carries on until it has faded out
	float target = 0.0f;
	if (type != TEST_SIG_OFF) {
		target = atomic_load_explicit(&g_gen_gain, memory_order_relaxed);
		if (type == TEST_SIG_SWEEP && g_gen_playing != TEST_SIG_SWEEP) {
			g_gen_sweep_freq = TEST_SIG_SWEEP_START;    // a new sweep starts at the bottom
		}
		g_gen_playing = type;
	}
	if (g_gen_playing == TEST_SIG_OFF) {
		return 0;                               // off, and faded out. Leave the buffer alone
	}
	if (type == TEST_SIG_OFF && g_gen_level < 1e-5f) {
		g_gen_playing = TEST_SIG_OFF;           // the fade out has finished (-100 dB)
		g_gen_level = 0.0f;
		return 0;
	}

	float rate = (float)sample_rate;
	float glide = 1.0f - expf(-1.0f / (rate * 0.001f * TEST_SIG_FADE_MS));     // the same one-pole smoothing as the envelope follower
	double step = (double)atomic_load_explicit(&g_gen_freq, memory_order_relaxed) / (double)rate;

	//The sweep is logarithmic: every sample, the frequency is multiplied by the same small amount,
	//so each octave takes the same time. glide_ratio is that amount
	double sweep_end = TEST_SIG_SWEEP_END;
	if (sweep_end > 0.45 * rate) {
		sweep_end = 0.45 * rate;
	}
	double sweep_samples = (double)atomic_load_explicit(&g_gen_sweep_s, memory_order_relaxed) * rate;
	double glide_ratio = pow(sweep_end / TEST_SIG_SWEEP_START, 1.0 / sweep_samples);

	for (int i = 0; i < count; i++) {

		float value;
		if (g_gen_playing == TEST_SIG_SINE || g_gen_playing == TEST_SIG_SWEEP) {
			//Done in double precision so the sine's own distortion (about -150 dB) is far below anything being measured
			value = (float)sin(TEST_SIG_TWO_PI * g_gen_phase);
			if (g_gen_playing == TEST_SIG_SWEEP) {
				//The phase carries on smoothly as the frequency changes, so the sweep never clicks,
				//not even when it jumps from the top back to the bottom
				g_gen_phase += g_gen_sweep_freq / rate;
				g_gen_sweep_freq *= glide_ratio;
				if (g_gen_sweep_freq >= sweep_end) {
					g_gen_sweep_freq = TEST_SIG_SWEEP_START;
				}
			}
			else {
				g_gen_phase += step;
			}
			if (g_gen_phase >= 1.0) {
				g_gen_phase -= 1.0;
			}
		}
		else {
			//White noise: a new random 32-bit number each sample (xorshift), scaled to -1.0 .. 1.0
			g_gen_random ^= g_gen_random << 13;
			g_gen_random ^= g_gen_random >> 17;
			g_gen_random ^= g_gen_random << 5;
			value = (float)(int32_t)g_gen_random * (1.0f / 2147483648.0f);

			if (g_gen_playing == TEST_SIG_PINK) {
				//Pink noise: white noise through three one-pole lowpass filters, mixed so the level
				//falls 3 dB per octave (Paul Kellet's "economy" filter). The last factor brings its peaks back to about 1.0
				g_gen_pink[0] = 0.99765f * g_gen_pink[0] + value * 0.0990460f;
				g_gen_pink[1] = 0.96300f * g_gen_pink[1] + value * 0.2965164f;
				g_gen_pink[2] = 0.57000f * g_gen_pink[2] + value * 1.0526913f;
				value = (g_gen_pink[0] + g_gen_pink[1] + g_gen_pink[2] + value * 0.1848f) * TEST_SIG_PINK_SCALE;
			}
		}

		g_gen_level += (target - g_gen_level) * glide;
		samples[i] = value * g_gen_level;
	}
	return 1;
}

// Reads a command (see THE COMMANDS in test_signals.h) and applies it
// Returns 1 if it was a valid command, 0 if it was ignored
int test_signal_command(const char *message) {

	char name[16];
	float freq, level_db;
	float sweep_s = TEST_SIG_SWEEP_DEFAULT_S;   // the 4th field is optional

	if (message == NULL) {
		return 0;
	}
	if (sscanf(message, "gen %15s %f %f %f", name, &freq, &level_db, &sweep_s) < 3) {
		return 0;                               //message string is malformed, ignore it
	}
	if (!isfinite(freq) || !isfinite(level_db) || !isfinite(sweep_s)) {
		return 0;                               //check for NaN or infinite values
	}

	int type;
	if (strcmp(name, "off") == 0)        { type = TEST_SIG_OFF; }
	else if (strcmp(name, "sine") == 0)  { type = TEST_SIG_SINE; }
	else if (strcmp(name, "white") == 0) { type = TEST_SIG_WHITE; }
	else if (strcmp(name, "pink") == 0)  { type = TEST_SIG_PINK; }
	else if (strcmp(name, "sweep") == 0) { type = TEST_SIG_SWEEP; }
	else {
		return 0;                               //not a type we know
	}

	//Keep the sine below half the sample rate (above that it would alias), and the level at or below full scale
	float highest = 0.45f * (float)atomic_load_explicit(&g_gen_sample_rate, memory_order_relaxed);
	if (freq < 1.0f) {
		freq = 1.0f;
	}
	if (freq > highest) {
		freq = highest;
	}
	if (level_db > 0.0f) {
		level_db = 0.0f;
	}
	if (level_db < -120.0f) {
		level_db = -120.0f;
	}
	if (sweep_s < 1.0f) {
		sweep_s = 1.0f;
	}
	if (sweep_s > 300.0f) {
		sweep_s = 300.0f;
	}

	//The type is stored last, so the generator never starts with the previous frequency or level
	atomic_store_explicit(&g_gen_freq, freq, memory_order_relaxed);
	atomic_store_explicit(&g_gen_gain, powf(10.0f, level_db / 20.0f), memory_order_relaxed);
	atomic_store_explicit(&g_gen_sweep_s, sweep_s, memory_order_relaxed);
	atomic_store_explicit(&g_gen_type, type, memory_order_relaxed);
	return 1;
}

void test_signal_off(void) {

	atomic_store_explicit(&g_gen_type, TEST_SIG_OFF, memory_order_relaxed);   // test_signal_process() fades it out
}

int test_signal_is_on(void) {

	return atomic_load_explicit(&g_gen_type, memory_order_relaxed) != TEST_SIG_OFF;
}
