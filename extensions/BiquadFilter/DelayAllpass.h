// Delay-line (Schroeder) Allpass Filter Module (companion to Comb.h)
//
// Transfer function:  H(z) = (-g + z^-M) / (1 - g*z^-M)
//
// Implemented with a single delay line (canonical form):
//   w[n] = x[n] + g*w[n-M]
//   y[n] = -g*w[n] + w[n-M]
//
// For a whole-number M the magnitude response is exactly 1 at every frequency;
// only the phase (and therefore the timing of the echoes) changes. This is the
// building block of Schroeder/Freeverb-style reverbs and of phaser/diffusion
// effects. Its impulse response is an echo train: -g at n=0, then (1-g^2) at M,
// g*(1-g^2) at 2M, and so on.
//
// With a fractional M (linear interpolation) the magnitude is only approximately
// flat. Low frequencies are essentially unaffected, but the high end can dip by
// several dB (e.g. about 3-4 dB at 10-20 kHz for M=43.6 at 48 kHz). If you need a
// flat response while modulating, keep M whole or use a better interpolator.
//
// Unlike the shelves and comb in this set, g is a LINEAR coefficient, not dB,
// because its sign matters and it is a coefficient in all the reverb literature.

typedef struct _delayallpass{
    float g;            // coefficient, -1 < g < 1
    float delay;        // delay in samples (M), possibly fractional
    float max_delay;    // longest delay the buffer can hold (set at creation)
    float* buffer;      // circular delay line holding w[n]
    int size;           // length of buffer
    int write_idx;      // slot for the current sample
    const char* type;
}delayallpass;

// frequency: sets M = sample_rate / frequency, in Hz, must be in (0, sample_rate/2)
// gain:      linear coefficient g, must satisfy |g| < 1 (typical reverb values: 0.5 - 0.7)
// Returns NULL on bad parameters or allocation failure.
delayallpass* da_new(float frequency,
                     float gain,
                     int sample_rate);

// Same, but you give the delay directly in samples (fractional allowed, >= 1).
// For milliseconds: delay_samples = ms * sample_rate / 1000.
delayallpass* da_new_delay(float delay_samples,
                           float gain);

// For modulated use: allocates the delay line for max_delay_samples up front,
// starting at delay_samples. Requires 1 <= delay_samples <= max_delay_samples.
delayallpass* da_new_max_delay(float delay_samples,
                               float max_delay_samples,
                               float gain);

// Change the current delay. Safe to call from the audio loop: it never
// allocates or prints. Returns 0 on success, or -1 (delay unchanged) if
// delay_samples is < 1, > max_delay, or NaN.
int da_set_delay(delayallpass* da, float delay_samples);

float da_process(delayallpass* da, float input);

void da_reset(delayallpass* da);   // clear the delay line

void da_destroy(delayallpass* da);

void da_print_info(delayallpass* da);
