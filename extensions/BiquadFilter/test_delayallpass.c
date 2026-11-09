#include "DelayAllpass.h"
#include <stdio.h>
#include <math.h>

// Steady-state gain in dB at an integer frequency 'hz': drive with a sine, wait
// for the echoes to die away, then measure RMS over exactly 1 second (a whole
// number of cycles, so the RMS is exact). A pure sine has RMS = amplitude/sqrt(2).
static double measure_db(delayallpass* da, int hz, double fs){
    da_reset(da);
    int settle = (int)(fs * 0.5), N = (int)fs;
    double acc = 0;
    for (int n = 0; n < settle + N; n++){
        float y = da_process(da, (float)sin(2*M_PI*hz*n/fs));
        if (n >= settle) acc += (double)y * y;
    }
    double rms = sqrt(acc / N);
    return 20*log10(rms * sqrt(2.0));
}

int main(void){
    double fs = 48000;

    printf("1) Flat magnitude, whole-number delay M=48, g=0.5 (expect 0.000 dB everywhere)\n");
    delayallpass* a = da_new_delay(48.0f, 0.5f);
    int freqs[] = {50, 300, 500, 1000, 1234, 4000, 9999, 20000, 23000};
    printf("  ");
    for (int i = 0; i < 9; i++) printf("%dHz: %.3f | ", freqs[i], measure_db(a, freqs[i], fs));
    printf("\n");
    da_destroy(a);

    printf("\n2) Impulse response, M=4, g=0.5 (expect -0.5 0 0 0 0.75 0 0 0 0.375 0 0 0 0.1875):\n  ");
    a = da_new_delay(4.0f, 0.5f);
    for (int n = 0; n < 13; n++) printf("%.4f ", da_process(a, n==0?1.0f:0.0f));
    printf("\n");
    da_destroy(a);

    printf("   Negative gain, g=-0.5 (expect 0.5 0 0 0 0.75 0 0 0 -0.375 0 0 0 0.1875):\n  ");
    a = da_new_delay(4.0f, -0.5f);
    for (int n = 0; n < 13; n++) printf("%.4f ", da_process(a, n==0?1.0f:0.0f));
    printf("\n");
    da_destroy(a);

    printf("\n3) Energy conservation: sum of squares of the impulse response (expect 1.000000)\n");
    a = da_new_delay(37.0f, 0.7f);
    double e = 0;
    for (int n = 0; n < 20000; n++){ float y = da_process(a, n==0?1.0f:0.0f); e += (double)y*y; }
    printf("  %.6f\n", e);
    da_destroy(a);

    printf("\n4) da_new(frequency) matches da_new_delay: da_new(1000 Hz) -> ");
    a = da_new(1000.0f, 0.5f, 48000);
    printf("delay = %.3f samples (expect 48.000), gain at 1000 Hz = %.3f dB\n",
           a->delay, measure_db(a, 1000, fs));
    da_destroy(a);

    printf("\n5) Fractional delay M=43.636 (linear interpolation): magnitude is only approximately flat\n  ");
    a = da_new_delay(43.636f, 0.5f);
    int ff[] = {100, 1000, 5000, 10000, 15000, 20000};
    for (int i = 0; i < 6; i++) printf("%dHz: %.3f | ", ff[i], measure_db(a, ff[i], fs));
    printf("\n  (never above 0 dB; negligible at low frequencies, but several dB down at the top end)\n");
    da_destroy(a);

    printf("\n6) Modulated delay: da_new_max_delay and da_set_delay\n");
    a = da_new_max_delay(10.0f, 10.0f, 0.5f);
    {
        // separate statements: printf argument evaluation order is unspecified in C
        int r1 = da_set_delay(a, 4.0f);
        int r2 = da_set_delay(a, 10.0f);
        int r3 = da_set_delay(a, 10.5f);
        int r4 = da_set_delay(a, 0.5f);
        int r5 = da_set_delay(a, NAN);
        printf("  set 4.0 -> %d, set 10.0 -> %d, set 10.5 -> %d, set 0.5 -> %d, set NaN -> %d (expect 0 0 -1 -1 -1)\n",
               r1, r2, r3, r4, r5);
    }
    printf("  delay after rejected calls: %.1f (expect 10.0)\n", a->delay);
    da_set_delay(a, 4.0f);
    printf("  impulse response after shortening to M=4 (expect -0.5 0 0 0 0.75):\n  ");
    for (int n = 0; n < 5; n++) printf("%.4f ", da_process(a, n==0?1.0f:0.0f));
    printf("\n");
    da_destroy(a);

    printf("\n7) Sweep: delay moved every sample, 0.2-5 ms, 0.5 Hz LFO, 4 s of 220 Hz sine, g=0.5\n");
    {
        double lo = 0.0002*fs, hi = 0.005*fs;
        delayallpass* sw = da_new_max_delay((float)lo, (float)hi, 0.5f);
        double peak = 0; int bad = 0;
        for (int n = 0; n < (int)(4*fs); n++){
            double lfo = 0.5 - 0.5*cos(2*M_PI*0.5*n/fs);
            if (da_set_delay(sw, (float)(lo + (hi-lo)*lfo)) != 0) bad++;
            float y = da_process(sw, (float)sin(2*M_PI*220.0*n/fs));
            if (!isfinite(y)) bad++;
            if (fabs(y) > peak) peak = fabs(y);
        }
        // |y| <= |g||w| + |d| and |w| <= 1/(1-g) = 2 for unit input, so y <= 0.5*2 + 2 = 3 at the very worst
        printf("  peak output %.3f (expect about 1, hard limit 3), invalid samples: %d (expect 0)\n", peak, bad);
        da_destroy(sw);
    }

    printf("\n8) Bad params: ");
    if (!da_new_delay(10.0f, 1.0f))  printf("(g=1 rejected) ");
    if (!da_new_delay(10.0f, -1.5f)) printf("(g=-1.5 rejected) ");
    if (!da_new_delay(10.0f, NAN))   printf("(g=NaN rejected) ");
    if (!da_new_delay(0.5f, 0.5f))   printf("(delay<1 rejected) ");
    if (!da_new_max_delay(8.0f, 5.0f, 0.5f)) printf("(max<delay rejected) ");
    if (!da_new(30000.0f, 0.5f, 48000)) printf("(freq>Nyquist rejected)\n");
    return 0;
}
