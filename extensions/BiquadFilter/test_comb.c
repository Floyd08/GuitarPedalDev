#define _USE_MATH_DEFINES

#include "comb.h"
#include <stdio.h>
#include <math.h>

// Steady-state gain in dB at 'hz' by driving with a sine and measuring peak output
static double measure_db(comb* cb, double hz, double fs){
    cb_reset(cb);
    int N = (int)(fs * 0.5);
    double peak = 0;
    for (int n = 0; n < N; n++){
        float y = cb_process(cb, (float)sin(2*M_PI*hz*n/fs));
        if (n > N/2 && fabs(y) > peak) peak = fabs(y);
    }
    return 20*log10(peak);
}

int main(void){
    double fs = 48000;
    double g_db = -6.0206;   // g = 0.5
    const char* names[] = {"FF","FF NEG","FB","FB NEG"};
    printf("M=48 (f0=1000 Hz), g=0.5. Expected FF: peak +3.52, notch -6.02 | FB: peak +6.02, trough -3.52\n");
    printf("%-8s %10s %10s %10s\n","type","f0","f0/2","2*f0");
    for (int t = 0; t < 4; t++){
        comb* c = cb_new(t, 1000, (float)g_db, (int)fs);
        printf("%-8s %10.2f %10.2f %10.2f\n", names[t],
               measure_db(c,1000,fs), measure_db(c,500,fs), measure_db(c,2000,fs));
        cb_destroy(c);
    }
    printf("\nFractional delay: f0=1100 Hz -> M=%.3f, FB g=0.5\n", fs/1100);
    comb* c = cb_new(COMB_FEEDBACK, 1100, (float)g_db, (int)fs);
    printf("  gain at 1100 Hz: %.2f dB (ideal +6.02), at 550 Hz: %.2f dB (ideal -3.52)\n",
           measure_db(c,1100,fs), measure_db(c,550,fs));
    cb_destroy(c);

    printf("\nImpulse response, FB g=0.5, M=4 (f0=12000):\n  ");
    c = cb_new(COMB_FEEDBACK, 12000, (float)g_db, 48000);
    for (int n=0;n<13;n++) printf("%.4f ", cb_process(c, n==0?1.0f:0.0f));
    printf("\n");
    cb_destroy(c);


    printf("\ncb_new_delay: same filter as cb_new(1000 Hz), given as M=48 samples\n");
    c = cb_new_delay(COMB_FEEDBACK, 48.0f, (float)g_db);
    printf("  gain at 1000 Hz: %.2f dB (expect 6.02), at 500 Hz: %.2f dB (expect -3.52)\n",
           measure_db(c,1000,fs), measure_db(c,500,fs));
    cb_destroy(c);

    printf("cb_new_delay: FF, M=3.5 samples, g=0.5, impulse response (expect 1, 0, 0, 0.25, 0.25):\n  ");
    c = cb_new_delay(COMB_FEEDFORWARD, 3.5f, (float)g_db);
    for (int n=0;n<5;n++) printf("%.4f ", cb_process(c, n==0?1.0f:0.0f));
    printf("\n");
    cb_destroy(c);

    printf("cb_new_delay: minimum delay M=1, FB g=0.5 (expect 1, 0.5, 0.25, 0.125):\n  ");
    c = cb_new_delay(COMB_FEEDBACK, 1.0f, (float)g_db);
    for (int n=0;n<4;n++) printf("%.4f ", cb_process(c, n==0?1.0f:0.0f));
    printf("\n");
    cb_destroy(c);

    printf("cb_new_delay bad params: ");
    if (!cb_new_delay(COMB_FEEDFORWARD, 0.5f, -6.0f)) printf("(delay < 1 rejected) ");
    if (!cb_new_delay(COMB_FEEDBACK, 10.0f, 0.0f)) printf("(FB 0 dB rejected)\n");

    printf("\nBad params: ");
    if (!cb_new(COMB_FEEDBACK, 1000, 0.0f, 48000)) printf("(FB 0 dB rejected) ");
    if (!cb_new(COMB_FEEDFORWARD, 30000, -6.0f, 48000)) printf("(freq > Nyquist rejected)\n");
    return 0;
}
