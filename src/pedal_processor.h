#include "distortion_effects.h"

#define SAMPLE_RATE			48000
#define FRAMES_PER_BUFFER	128
#define NUM_IN_CHANNELS		1                           //Guitars are Mono
#define NUM_OUT_CHANNELS    2                           //Ears are Stereo
#define SILENCE             0.0f
#define NUM_EFFECTS         12                          //Chosen arbitrarily, will likely rise

float passthrough(float signal);
void passthrough_buffer(int buf_size, float* in_buf, float* out_buf);
float basic_clipping(float drive, float signal);
float simple_soft_clipping(float drive, float signal);

void build_fav_distort_1(distortion_engine* d_engine, int up_factor, int sample_rate, int buffer_size, float* up_buf);