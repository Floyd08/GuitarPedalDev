#include "effects_engine.h"
#include "control_def.h"
#include "master_effects.h"
#include "dsp_tools.h"

const ControlDef master_controls[MASTER_NUM_CONTROLS] = {
	    [MASTER_VOLUME] = { .name = "volume", .label = "Volume", .type = CTRL_KNOB, .row = 0, .default_value = 0.5f, .update_func = set_master_volume }
    };

void master_effects_init(master_effects_engine* master_engine, int buffer_size) {

    effects_engine base;
	effects_engine_init(&base, "master_effects");
    base.controls = master_controls;
    base.num_controls = MASTER_NUM_CONTROLS;

    master_engine->base = base;
	master_engine->buffer_size = buffer_size;
	master_engine->master_volume = linear_to_log(DFLT_VOL, minus_60dB, _0dB);
    //#define		minus_60dB			0.001
    //#define		_0dB				1
}

void set_master_volume (effects_engine* engine, float volume) {

	master_effects_engine* master_engine = (master_effects_engine*)engine;

	master_engine->master_volume = linear_to_log(volume, minus_60dB, _0dB);
}

void process_master_volume(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

    master_effects_engine* master_engine = (master_effects_engine*)engine;

    int i = 0;
    while (i < master_engine->buffer_size) {

        out_buf[i] = in_buf[i] * master_engine->master_volume;
        i++;
    }
}