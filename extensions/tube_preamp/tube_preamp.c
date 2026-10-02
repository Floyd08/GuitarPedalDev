/*
 * tube_preamp.c
 *
 * Implementation of the tube preamp module declared in tube_preamp.h.
 */

#include "tube_preamp.h"
#include <math.h>

void tube_preamp_init(TubePreamp *p, double drive, double asymmetry,
                       double lowpass_cutoff_hz, double highpass_cutoff_hz,
                       double output_level, double sample_rate) {
    p->drive = drive;
    p->asymmetry = asymmetry;

    /* Precompute a normalization factor so raising 'drive' doesn't also
     * silently change the output volume - tanh saturates toward the
     * positive-half ceiling, so we scale by that ceiling's value. */
    p->out_norm = 1.0 / tanh(drive * (1.0 + asymmetry));

    p->dc_prev_in = 0.0;
    p->dc_prev_out = 0.0;
    p->dc_R = 0.995;

    p->lpf_prev_out = 0.0;
    /* Standard one-pole coefficient: coeff = exp(-2*pi*cutoff/sample_rate) */
    p->lpf_coeff = exp(-2.0 * M_PI * lowpass_cutoff_hz / sample_rate);

    p->hpf_prev_in = 0.0;
    p->hpf_prev_out = 0.0;
    p->hpf_coeff = exp(-2.0 * M_PI * highpass_cutoff_hz / sample_rate);

    p->output_level = output_level;
}

/* Asymmetric tanh saturation - the positive and negative halves of the
 * waveform see slightly different effective drive, which is what makes
 * a tube stage sound different from a simple symmetric clipper: it adds
 * even-order harmonics (2nd, 4th...) on top of the odd-order harmonics
 * (3rd, 5th...) that symmetric saturation alone produces. */
static double saturate(double x, double drive, double asymmetry, double out_norm) {
    double driven = x * drive;
    double y;
    if (driven >= 0.0) {
        y = tanh(driven * (1.0 + asymmetry));
    } else {
        y = tanh(driven * (1.0 - asymmetry));
    }
    return y * out_norm;
}

float tube_preamp_process(TubePreamp *p, float input_sample) {
    double x = (double)input_sample;

    /* Stage 1+2: drive into asymmetric saturation */
    double saturated = saturate(x, p->drive, p->asymmetry, p->out_norm);

    /* Stage 3: DC blocker - classic one-pole high-pass at a very low
     * cutoff, specifically to remove the DC offset that asymmetric
     * clipping introduces (the waveform is no longer centered on zero) */
    double dc_out = saturated - p->dc_prev_in + p->dc_R * p->dc_prev_out;
    p->dc_prev_in = saturated;
    p->dc_prev_out = dc_out;

    /* Stage 4: post low-pass - tames the harsh upper harmonics that
     * heavy saturation generates, same one-pole idea as our very first
     * DSP example, just tuned much higher (kHz range) */
    double lpf_out = (1.0 - p->lpf_coeff) * dc_out + p->lpf_coeff * p->lpf_prev_out;
    p->lpf_prev_out = lpf_out;

    /* Stage 5: post high-pass - clears out sub-bass mud below where a
     * guitar's fundamental actually lives */
    double hpf_out = p->hpf_coeff * (p->hpf_prev_out + lpf_out - p->hpf_prev_in);
    p->hpf_prev_in = lpf_out;
    p->hpf_prev_out = hpf_out;

    /* Stage 6: output level */
    double output = hpf_out * p->output_level;

    if (output > 1.0) output = 1.0;
    if (output < -1.0) output = -1.0;

    return (float)output;
}
