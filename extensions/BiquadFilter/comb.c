#include "comb.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Shared constructor: validates the type and gain, then allocates the struct
// and delay line for a delay of 'delay' samples (may be fractional, >= 1).
static comb* cb_create(int filter_type, double delay, float dbGain){

    int feedback = 0;
    double sign = 1.0;
    const char* name = NULL;

    switch (filter_type){
    case COMB_FEEDFORWARD:     feedback = 0; sign =  1.0; name = "COMB FEEDFORWARD";     break;
    case COMB_FEEDFORWARD_NEG: feedback = 0; sign = -1.0; name = "COMB FEEDFORWARD NEG"; break;
    case COMB_FEEDBACK:        feedback = 1; sign =  1.0; name = "COMB FEEDBACK";        break;
    case COMB_FEEDBACK_NEG:    feedback = 1; sign = -1.0; name = "COMB FEEDBACK NEG";    break;
    default:
        printf("Error: unknown comb filter type\n");
        return NULL;
    }

    double g = pow(10.0, dbGain / 20.0);
    if (feedback && g >= 1.0){
        printf("Error: feedback comb requires dbGain < 0 (unstable otherwise)\n");
        return NULL;
    }

    comb* tmp = (comb*)malloc(sizeof(comb));
    if (tmp == NULL){
        printf("Error while allocating memory for comb");
        return tmp;
    }

    // Linear interpolation reads samples n-floor(M) and n-floor(M)-1,
    // so keep two extra slots.
    tmp->size = (int)ceil(delay) + 2;
    tmp->buffer = (float*)calloc(tmp->size, sizeof(float));
    if (tmp->buffer == NULL){
        printf("Error while allocating memory for comb delay line");
        free(tmp);
        return NULL;
    }

    tmp->g = (float)(sign * g);
    tmp->delay = (float)delay;
    tmp->feedback = feedback;
    tmp->write_idx = 0;
    tmp->type = name;

    return tmp;
}

comb* cb_new(int filter_type,
             float frequency,
             float dbGain,
             int sample_rate){

    if (frequency <= 0.0f || frequency >= sample_rate / 2.0f){
        printf("Error: frequency must be between 0 and Nyquist\n");
        return NULL;
    }

    return cb_create(filter_type, (double)sample_rate / frequency, dbGain);
}

comb* cb_new_delay(int filter_type,
                   float delay_samples,
                   float dbGain){

    // The delay must be finite and at least one sample (a feedback comb
    // can't read its own output from the current sample).
    if (!(delay_samples >= 1.0f) || delay_samples > 1.0e8f){
        printf("Error: delay must be at least 1 sample\n");
        return NULL;
    }

    return cb_create(filter_type, (double)delay_samples, dbGain);
}

float cb_process(comb* cb, float input){
    // Fractional read of the sample M steps back (linear interpolation)
    int   whole = (int)cb->delay;
    float frac  = cb->delay - (float)whole;

    int i0 = cb->write_idx - whole;
    int i1 = i0 - 1;
    if (i0 < 0) i0 += cb->size;
    if (i1 < 0) i1 += cb->size;

    float delayed = (1.0f - frac) * cb->buffer[i0] + frac * cb->buffer[i1];

    float output = input + cb->g * delayed;

    // Feedforward stores its input, feedback stores its output
    cb->buffer[cb->write_idx] = cb->feedback ? output : input;
    if (++cb->write_idx >= cb->size) cb->write_idx = 0;

    return output;
}

void cb_reset(comb* cb){
    for (int i = 0; i < cb->size; i++) cb->buffer[i] = 0.0f;
    cb->write_idx = 0;
}

void cb_destroy(comb* cb){
    if (cb == NULL) return;
    free(cb->buffer);
    free(cb);
}

void cb_print_info(comb* cb){
    printf("TYPE: %s\n", cb->type);
    printf("G: %.8f\n", cb->g);
    printf("Delay (samples): %.8f\n", cb->delay);
    printf("Buffer size: %d\n", cb->size);
}
