/*
 * test_tube_preamp.c
 *
 * Feeds a clean sine wave through the tube preamp and measures energy
 * at the fundamental and its harmonics (via the Goertzel algorithm,
 * same technique used to verify the low-pass filter earlier) to prove
 * the saturation stage is actually generating harmonic content, not
 * just changing volume.
 *
 * Build:
 *   gcc -O2 -o test_tube_preamp test_tube_preamp.c tube_preamp.c -lm
 */

#include <stdio.h>
#include <math.h>
#include "tube_preamp.h"

#define SAMPLE_RATE 48000
#define NUM_SAMPLES (SAMPLE_RATE * 1) /* 1 second */
#define FUNDAMENTAL_HZ 220.0          /* A3, a typical guitar open-string range */

static double goertzel_power(const double *samples, int n, double freq, double sample_rate) {
    int k = (int)(0.5 + n * freq / sample_rate);
    double w = 2.0 * M_PI * k / n;
    double coeff = 2.0 * cos(w);
    double s_prev = 0.0, s_prev2 = 0.0;
    for (int i = 0; i < n; i++) {
        double s = samples[i] + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }
    return s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
}

int main(void) {
    static double dry[NUM_SAMPLES];
    static double wet[NUM_SAMPLES];

    TubePreamp preamp;
    tube_preamp_init(&preamp,
                      /*drive=*/        6.0,   /* pushed hard, well into saturation */
                      /*asymmetry=*/    0.3,   /* tube-like asymmetric clipping */
                      /*lowpass_hz=*/   7000.0,
                      /*highpass_hz=*/  80.0,
                      /*output_level=*/ 0.8,
                      SAMPLE_RATE);

    for (int i = 0; i < NUM_SAMPLES; i++) {
        double t = (double)i / SAMPLE_RATE;
        /* A moderately hot input signal, like a humbucker digging in */
        double sample = 0.5 * sin(2.0 * M_PI * FUNDAMENTAL_HZ * t);
        dry[i] = sample;
        wet[i] = tube_preamp_process(&preamp, (float)sample);
    }

    printf("Fundamental: %.0f Hz\n\n", FUNDAMENTAL_HZ);
    printf("%-12s %-18s %-18s\n", "Harmonic", "Dry power", "Wet (saturated) power");
    for (int h = 1; h <= 5; h++) {
        double freq = FUNDAMENTAL_HZ * h;
        double dry_p = goertzel_power(dry, NUM_SAMPLES, freq, SAMPLE_RATE);
        double wet_p = goertzel_power(wet, NUM_SAMPLES, freq, SAMPLE_RATE);
        printf("%dx (%5.0fHz) %-18.3e %-18.3e\n", h, freq, dry_p, wet_p);
    }

    printf("\nIf saturation is working: the dry signal should show power\n");
    printf("essentially only at 1x (a pure sine has no harmonics). The wet\n");
    printf("signal should show real energy appearing at 2x, 3x, 4x, 5x -\n");
    printf("harmonic content that didn't exist before, generated purely by\n");
    printf("the nonlinear saturation stage.\n");

    return 0;
}
