#include "effects_engine.h"
#include "control_def.h"
#include "master_effects.h"
#include "dsp_tools.h"

const ControlDef master_controls[MASTER_NUM_CONTROLS] = {
	    [MASTER_VOLUME] = { .name = "volume", .label = "Volume", .type = CTRL_KNOB, .row = MASTER_FX_ROW_NUM, .default_value = DFLT_VOL, .update_func = set_master_volume, .units = "dB" }
    };

void master_effects_init(master_effects_engine* master_engine, int buffer_size) {

    effects_engine base;
	effects_engine_init(&base, "master_effects");
    base.controls = master_controls;
    base.num_controls = MASTER_NUM_CONTROLS;
    base.colour = "#DC143C";                    // Crimson

    master_engine->base = base;
	master_engine->buffer_size = buffer_size;
    set_master_volume(&master_engine->base, DFLT_VOL);
	//master_engine->master_volume = linear_to_log(DFLT_VOL, minus_60dB, _0dB);
    //#define		minus_60dB			0.001
    //#define		_0dB				1
}

float set_master_volume(effects_engine* engine, float volume) {

	master_effects_engine* master_engine = (master_effects_engine*)engine;

    float new_volume = linear_to_log(volume, minus_60dB, _0dB);
	master_engine->master_volume = new_volume;

    return linear_gain_to_decibels(new_volume);
}

void process_master_volume(effects_engine* engine, float* in_buf, float* out_buf, int buffer_size) {

    master_effects_engine* master_engine = (master_effects_engine*)engine;

    int i = 0;
    while (i < buffer_size) {

        out_buf[i] = in_buf[i] * master_engine->master_volume;
        i++;
    }
}