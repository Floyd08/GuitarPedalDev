#ifndef TUBE_PREAMP_H
#define TUBE_PREAMP_H

/*
 * tube_preamp.h
 *
 * A simple tube-style preamp emulation:
 *   input gain (drive) -> asymmetric saturation -> DC blocker
 *   -> low-pass (tames saturation fizz) -> high-pass (removes sub-bass mud)
 *   -> output level
 *
 * Each stage maps to a concept from earlier in this project:
 *   - saturation is nonlinear waveshaping (new)
 *   - the DC blocker and tone filters are one-pole IIR filters,
 *     the same building block as compressor.c's envelope follower
 *     and the very first low-pass filter example.
 */

typedef struct {
    /* Stage 1: drive */
    double drive;       /* linear gain applied before saturation */
    double asymmetry;   /* 0.0 = symmetric clipping, >0 = tube-like asymmetry */
    double out_norm;    /* normalizes saturated output back near unity */

    /* Stage 2: DC blocker (removes the DC offset asymmetric clipping adds) */
    double dc_prev_in;
    double dc_prev_out;
    double dc_R;         /* close to 1.0, e.g. 0.995 */

    /* Stage 3: post low-pass (tames harsh high-frequency harmonics) */
    double lpf_prev_out;
    double lpf_coeff;

    /* Stage 4: post high-pass (removes sub-bass mud) */
    double hpf_prev_in;
    double hpf_prev_out;
    double hpf_coeff;

    /* Stage 5: output level */
    double output_level;
} TubePreamp;

/* sample_rate is needed to convert the low/high-pass cutoffs (in Hz)
 * into the correct per-sample filter coefficients. */
void tube_preamp_init(TubePreamp *p, double drive, double asymmetry,
                       double lowpass_cutoff_hz, double highpass_cutoff_hz,
                       double output_level, double sample_rate);

float tube_preamp_process(TubePreamp *p, float input_sample);

#endif /* TUBE_PREAMP_H */
