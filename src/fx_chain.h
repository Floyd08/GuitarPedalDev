#ifndef FX_CHAIN_H
#define FX_CHAIN_H

#include "effects_engine.h"

#define NUM_EFFECTS         12                          //Chosen arbitrarily, will likely rise

typedef struct {

	effects_engine* engine;							// The effects object needed by the processing function
	prcs_function process_func;						// The processing function itself

} fx_module;

typedef struct {

	fx_module slots[NUM_EFFECTS];
	int module_count;

} fx_chain;

void fx_chain_init(fx_chain* chain);
int fx_chain_append(fx_chain* chain, effects_engine* engine, prcs_function process_func);
void process_fx_chain(fx_chain* chain, const float* in_buf, float* out_buf, int buffer_size);

void mono_to_stereo(float* out_buffer, int mono_size);

#endif