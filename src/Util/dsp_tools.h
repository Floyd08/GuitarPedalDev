#ifndef DSP_TOOLS_H
#define DSP_TOOLS_H

#include "Biquad.h"
#include "distortion_effects.h"

biquad* alias_init(int upsample_factor, int sample_rate);
void alias_terminate(biquad* alias_filter);
biquad* leading_HPF_init(int up_factor, int sample_rate);
void leading_HPF_terminate(biquad* leading_HPF);
biquad* trailing_LPF_initint(int up_factor, int sample_rate);
void trailing_LPF_terminate(biquad* trailing_LPF);

void upsample(float* input_buf, int buffer_size, float* upsampled_buf, int upsample_factor);
void downsample_mono(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);
void downsample_stereo(float* upsampled_buf, int upsample_factor, float* output_buf, int buffer_size);

void alias_filter_process(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);
void alias_filter_process_gc(biquad* alias_filter, int buffer_size, float* upsampled_buffer, int upsample_factor);
void leading_HPF_process(biquad* leading_HPF, int buffer_size, float* upsampled_buffer, int upsample_factor);
void trailing_LPF_process(biquad* trailing_LPF, int buffer_size, float* upsampled_buffer, int upsample_factor);

void add_bias(float bias, int buffer_size, float* upsampled_buffer, int upsample_factor);
float compute_bias_offset(distortion_effect_bysample distort_func, float bias);
void filter_bias(float bias_offset, int buffer_size, float* upsampled_buffer, int upsample_factor);

void apply_gain(float gain, int buf_size, float* up_buf, int up_factor);

#endif