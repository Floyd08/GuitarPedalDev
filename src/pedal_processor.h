#include "effects_engine.h"
#include "distortion_effects.h"

#define SAMPLE_RATE			48000
#define FRAMES_PER_BUFFER	128
#define NUM_IN_CHANNELS		1                           //Guitars are Mono
#define NUM_OUT_CHANNELS    2                           //Ears are Stereo
#define SILENCE             0.0f
#define NUM_EFFECTS         12                          //Chosen arbitrarily, will likely rise

typedef void (*prcs_function)(effects_engine*, float* , float*, int);

typedef struct {

	effects_engine* engine;								// The effects object needed by the processing function
	prcs_function process_func;						// The processing function itself

} fx_module;

typedef struct {

	fx_module slots[NUM_EFFECTS];
	int module_count;

} fx_chain;

float passthrough(float signal);
void passthrough_buffer(int buf_size, float* in_buf, float* out_buf);

void mono_to_stereo(float* out_buffer, int mono_size);

void fx_chain_init(fx_chain* chain);
int fx_chain_append(fx_chain* chain, effects_engine* engine, prcs_function process_func);
void process_fx_chain(fx_chain* chain, float* in_buf, float* out_buf, int buffer_size);

float basic_clipping(float drive, float signal);
float simple_soft_clipping(float drive, float signal);