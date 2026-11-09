// Comb Filter Module (companion to Biquad.h / OnePole.h)
//
// Unlike a biquad, a comb filter needs a delay line of ~sample_rate/frequency
// samples, so it owns a circular buffer that is allocated in cb_new().
//
//   Feedforward:  y[n] = x[n] + s*g*x[n-M]     (FIR, always stable)
//   Feedback:     y[n] = x[n] + s*g*y[n-M]     (IIR, stable only if g < 1)
//
// M = sample_rate / frequency (may be fractional; linear interpolation is used)
// g = 10^(dbGain/20)          s = +1 or -1 depending on the type
//
// Positive polarity: peaks at k*f (feedback / feedforward) and, for feedforward,
// notches at odd multiples of f/2. Negative polarity swaps peaks and notches.

typedef struct _comb{
    float g;            // signed feedback / feedforward gain (polarity included)
    float delay;        // delay in samples (M), possibly fractional
    int feedback;       // 1 = feedback (IIR), 0 = feedforward (FIR)
    float* buffer;      // circular delay line
    int size;           // length of buffer
    int write_idx;      // slot for the current sample
    const char* type;
}comb;

typedef enum {
    COMB_FEEDFORWARD     = 0,
    COMB_FEEDFORWARD_NEG = 1,
    COMB_FEEDBACK        = 2,
    COMB_FEEDBACK_NEG    = 3,
} COMB_TYPES;

// frequency: fundamental (first peak) in Hz, must be in (0, sample_rate/2)
// dbGain:    comb depth; linear gain g = 10^(dbGain/20).
//            Feedback types require dbGain < 0 (g < 1).
// Returns NULL on bad parameters or allocation failure.
comb* cb_new(int filter_type,
             float frequency,
             float dbGain,
             int sample_rate);

// Same as cb_new, but you give the delay directly instead of a frequency.
// delay_samples: delay M in samples (fractional values allowed), must be >= 1.
//                The equivalent fundamental is sample_rate / delay_samples.
//                For milliseconds: delay_samples = ms * sample_rate / 1000.
// dbGain:        as in cb_new (feedback types require dbGain < 0).
// Returns NULL on bad parameters or allocation failure.
comb* cb_new_delay(int filter_type,
                   float delay_samples,
                   float dbGain);

float cb_process(comb* cb, float input);

void cb_reset(comb* cb);   // clear the delay line

void cb_destroy(comb* cb);

void cb_print_info(comb* cb);
