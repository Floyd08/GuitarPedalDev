#include "effects_engine.h"

#define SAMPLE_RATE			48000
#define FRAMES_PER_BUFFER	128
#define NUM_IN_CHANNELS		1                           //Guitars are Mono
#define NUM_OUT_CHANNELS    2                           //Ears are Stereo
#define SILENCE             0.0f
#define SMTHER_ATCK_MS		10


float passthrough(float signal);
void passthrough_buffer(int buf_size, float* in_buf, float* out_buf);

float basic_clipping(float drive, float signal);
float simple_soft_clipping(float drive, float signal);