#ifndef DSP_TOOLS_H
#define DSP_TOOLS_H

#include "Biquad.h"
#include "distortion_effects.h"

biquad* alias_init(int upsample_factor, int sample_rate);
void alias_terminate(biquad* alias_filter);

void upsample(float* input_buf, int buffer_size, float* upsampled_buf, int upsample_factor);
void downsample_mono(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);
void downsample_stereo(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);

void alias_filter_process(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);
void alias_filter_process_gc(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);

#endif