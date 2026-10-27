#include "OnePole.h"
#include <stdio.h>
#include <math.h>
#include <complex.h>

static double mag_db(onepole* f, double hz, double fs){
    double w = 2*M_PI*hz/fs;
    double complex z1 = cexp(-I*w);
    double complex H = (f->b0 + f->b1*z1) / (1.0 + f->a1*z1);
    return 20*log10(cabs(H));
}
static double phase_deg(onepole* f, double hz, double fs){
    double w = 2*M_PI*hz/fs;
    double complex z1 = cexp(-I*w);
    double complex H = (f->b0 + f->b1*z1) / (1.0 + f->a1*z1);
    return carg(H)*180/M_PI;
}

int main(void){
    double fs = 48000, f0 = 1000;
    const char* names[] = {"LP","HP","AP","LS +6dB","HS +6dB","LS -6dB","HS -6dB"};
    int types[] = {OP_LOWPASS,OP_HIGHPASS,OP_ALLPASS,OP_LOWSHELF,OP_HIGHSHELF,OP_LOWSHELF,OP_HIGHSHELF};
    float gains[] = {0,0,0,6,6,-6,-6};
    printf("%-8s %9s %9s %9s %9s %9s\n","type","DC dB","f0 dB","Nyq dB","f0 phase","");
    for (int i=0;i<7;i++){
        onepole* f = op_new(types[i], f0, gains[i], (int)fs);
        printf("%-8s %9.3f %9.3f %9.3f %9.2f\n", names[i],
               mag_db(f,1e-3,fs), mag_db(f,f0,fs), mag_db(f,fs/2-1e-3,fs), phase_deg(f,f0,fs));
        op_destroy(f);
    }
    // time-domain sanity: LP step response settles to 1
    onepole* lp = op_new(OP_LOWPASS, 1000, 0, 48000);
    float y=0; for(int n=0;n<2000;n++) y=op_process(lp,1.0f);
    printf("LP step response after 2000 samples: %.6f\n", y);
    op_destroy(lp);
    printf("bad freq -> %p\n", (void*)op_new(OP_LOWPASS, 30000, 0, 48000));
    return 0;
}
