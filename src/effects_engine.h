#ifndef FX_ENGINE_H
#define FX_ENGINE_H

#include "control_def.h"

struct effects_engine {

	const char* name;
	const ControlDef* controls;
	int num_controls;

};

// A wrapper struct, so effects modules can return their own control defs
typedef struct {

	ControlDef* controls;
	int num_controls;

} control_info;

void effects_engine_init(effects_engine* base, char* name);
//control_info get_control_info(effects_engine* engine);

#endif