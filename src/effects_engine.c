#include <stdio.h>
#include "effects_engine.h"

void effects_engine_init(effects_engine* base, char* name) {

	base->name = name;
	base->controls = NULL;
	base->num_controls = 0;
}

// control_info get_control_info(effects_engine* engine) {

// 	control_info ci = {.controls = engine->controls, .num_controls = engine->num_controls};
// 	return ci;
// }
