#include "effects_engine.h"
#include "control_def.h"

#define DFLT_VOL	0.5				// The default position of the Master Volume knob

enum {
	MASTER_VOLUME,
	MASTER_NUM_CONTROLS
}; 
extern const ControlDef master_controls[MASTER_NUM_CONTROLS];

typedef struct {

	effects_engine base;				// included to imitate a supertype
	int buffer_size;					// The size of the buffer used by the audio stream
	float master_volume;				// Master output volume

} master_effects_engine;

void master_effects_init(master_effects_engine* master_engine, int buffer_size);
void set_master_volume (effects_engine* engine, float volume);
void process_master_volume(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size);