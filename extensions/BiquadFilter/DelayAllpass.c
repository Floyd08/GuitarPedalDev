#include "DelayAllpass.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Shared constructor: validates the gain, then allocates the struct and delay
// line. The initial delay is 'delay' samples (may be fractional, >= 1); the
// buffer is sized for 'max_delay' samples (>= delay).
static delayallpass* da_create(double delay, double max_delay, float gain){

    // Written so that NaN is rejected too
    if (!(fabsf(gain) < 1.0f)){
        printf("Error: allpass gain must satisfy |g| < 1 (unstable otherwise)\n");
        return NULL;
    }

    delayallpass* tmp = (delayallpass*)malloc(sizeof(delayallpass));
    if (tmp == NULL){
        printf("Error while allocating memory for delay allpass");
        return tmp;
    }

    // Linear interpolation reads samples n-floor(M) and n-floor(M)-1,
    // so keep two extra slots.
    tmp->size = (int)ceil(max_delay) + 2;
    tmp->buffer = (float*)calloc(tmp->size, sizeof(float));
    if (tmp->buffer == NULL){
        printf("Error while allocating memory for delay allpass delay line");
        free(tmp);
        return NULL;
    }

    tmp->g = gain;
    tmp->delay = (float)delay;
    tmp->max_delay = (float)max_delay;
    tmp->write_idx = 0;
    tmp->type = "DELAY ALLPASS";

    return tmp;
}

delayallpass* da_new(float frequency,
                     float gain,
                     int sample_rate){

    if (frequency <= 0.0f || frequency >= sample_rate / 2.0f){
        printf("Error: frequency must be between 0 and Nyquist\n");
        return NULL;
    }

    double M = (double)sample_rate / frequency;
    return da_create(M, M, gain);
}

delayallpass* da_new_delay(float delay_samples,
                           float gain){

    // The delay must be finite and at least one sample (the filter can't
    // read its own state from the current sample).
    if (!(delay_samples >= 1.0f) || delay_samples > 1.0e8f){
        printf("Error: delay must be at least 1 sample\n");
        return NULL;
    }

    return da_create((double)delay_samples, (double)delay_samples, gain);
}

delayallpass* da_new_max_delay(float delay_samples,
                               float max_delay_samples,
                               float gain){

    if (!(delay_samples >= 1.0f) || !(max_delay_samples >= delay_samples) ||
        max_delay_samples > 1.0e8f){
        printf("Error: need 1 <= delay <= max_delay\n");
        return NULL;
    }

    return da_create((double)delay_samples, (double)max_delay_samples, gain);
}

// No printing here: this is meant to be called from the audio loop,
// often once per sample, so errors are reported through the return value.
int da_set_delay(delayallpass* da, float delay_samples){
    if (!(delay_samples >= 1.0f) || delay_samples > da->max_delay)
        return -1;                      // out of range (or NaN): delay unchanged

    da->delay = delay_samples;
    return 0;
}

float da_process(delayallpass* da, float input){
    // Fractional read of w[n-M] (linear interpolation)
    int   whole = (int)da->delay;
    float frac  = da->delay - (float)whole;

    int i0 = da->write_idx - whole;
    int i1 = i0 - 1;
    if (i0 < 0) i0 += da->size;
    if (i1 < 0) i1 += da->size;

    float delayed = (1.0f - frac) * da->buffer[i0] + frac * da->buffer[i1];

    float w      = input + da->g * delayed;     // w[n] = x[n] + g*w[n-M]
    float output = -da->g * w + delayed;        // y[n] = -g*w[n] + w[n-M]

    da->buffer[da->write_idx] = w;
    if (++da->write_idx >= da->size) da->write_idx = 0;

    return output;
}

void da_reset(delayallpass* da){
    for (int i = 0; i < da->size; i++) da->buffer[i] = 0.0f;
    da->write_idx = 0;
}

void da_destroy(delayallpass* da){
    if (da == NULL) return;
    free(da->buffer);
    free(da);
}

void da_print_info(delayallpass* da){
    printf("TYPE: %s\n", da->type);
    printf("G: %.8f\n", da->g);
    printf("Delay (samples): %.8f\n", da->delay);
    printf("Max delay (samples): %.8f\n", da->max_delay);
    printf("Buffer size: %d\n", da->size);
}
