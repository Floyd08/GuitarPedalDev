#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "one_pole.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

onepole* op_new(int filter_type, float frequency, float dbGain, int sample_rate) {

    if (frequency <= 0.0f || frequency >= sample_rate / 2.0f){
        printf("Error: frequency must be between 0 and Nyquist\n");
        return NULL;
    }

    onepole* tmp = (onepole*)malloc(sizeof(onepole));
    if (tmp == NULL){
        printf("Error while allocating memory for onepole");
        return tmp;
    }

    // Helper variables (computed in double; K blows up near Nyquist)
    
    double A = pow(10.0, dbGain / 40.0);                 // sqrt of linear gain
    double K = tan(M_PI * frequency / sample_rate);      // pre-warped bilinear constant

    op_load_coefficients(tmp, filter_type, A, K);

    tmp->prev_input = 0.0f;
    tmp->prev_output = 0.0f;

    return tmp;
}

float op_process(onepole* op, float input) {

    if(op == NULL) {
        fprintf(stderr, "one pole filter pointer was null\n");
    }

    float output = (op->b0 * input) +
                   (op->b1 * op->prev_input) -
                   (op->a1 * op->prev_output);

    op->prev_input = input;
    op->prev_output = output;

    return output;
}

void op_destroy(onepole* op){
    free(op);
}

void op_print_info(onepole* op){
    printf("A1: %.8f\n", op->a1);
    printf("B0: %.8f\n", op->b0);
    printf("B1: %.8f\n", op->b1);
    printf("TYPE: %s\n", op->type);
    printf("Last input: %.8f\n", op->prev_input);
    printf("Last output: %.8f\n", op->prev_output);
}

// Coefficients come from bilinear-transforming an analog prototype with
// s' = s/wc  ->  (1/K) * (1 - z^-1) / (1 + z^-1),  K = tan(wc/2).
// All coefficients are returned already normalized by a0.
void op_load_coefficients(onepole* op, int filter_type, double A, double K){
    double a0;

    switch (filter_type){
    case OP_LOWPASS:                    // H(s') = 1 / (s' + 1)
        a0 = 1.0 + K;
        op->b0 = K / a0;
        op->b1 = K / a0;
        op->a1 = (K - 1.0) / a0;
        op->type = "LOWPASS (1st order)";
        break;

    case OP_HIGHPASS:                   // H(s') = s' / (s' + 1)
        a0 = 1.0 + K;
        op->b0 =  1.0 / a0;
        op->b1 = -1.0 / a0;
        op->a1 = (K - 1.0) / a0;
        op->type = "HIGHPASS (1st order)";
        break;

    case OP_ALLPASS:                    // H(s') = (1 - s') / (1 + s')
        a0 = 1.0 + K;
        op->b0 = (K - 1.0) / a0;
        op->b1 = 1.0;
        op->a1 = (K - 1.0) / a0;
        op->type = "ALLPASS (1st order)";
        break;

    case OP_LOWSHELF:                   // H(s') = (s' + A) / (s' + 1/A)
        a0 = 1.0 + K / A;               // DC gain = A^2, HF gain = 1
        op->b0 = (1.0 + A * K) / a0;
        op->b1 = (A * K - 1.0) / a0;
        op->a1 = (K / A - 1.0) / a0;
        op->type = "LOWSHELF (1st order)";
        break;

    case OP_HIGHSHELF:                  // H(s') = A^2 (s' + 1/A) / (s' + A)
        a0 = 1.0 + A * K;               // DC gain = 1, HF gain = A^2
        op->b0 = (A * A + A * K) / a0;
        op->b1 = (A * K - A * A) / a0;
        op->a1 = (A * K - 1.0) / a0;
        op->type = "HIGHSHELF (1st order)";
        break;

    default:
        op->b0 = 1.0; op->b1 = 0.0; op->a1 = 0.0;   // pass-through
        op->type = "UNKNOWN";
        break;
    }
}
