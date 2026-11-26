#include <string.h>
#include "fx_chain.h"
#include "control_def.h"
#include "ui_listener.h"                                // register_controls
#include "scope_tap.h"                                  // scope_test_signal, scope_write

void fx_chain_init(fx_chain* chain) {

    chain->module_count = 0;
}

int fx_chain_append(fx_chain* chain, effects_engine* engine, prcs_function process_func) {

    int ctrl_err = 0;                                   // stays 0 for modules with no controls

    if(chain->module_count >= NUM_EFFECTS || engine == NULL || process_func == NULL) {
        return 1;
    }

    chain->slots[chain->module_count].engine = engine;
    chain->slots[chain->module_count].process_func = process_func;
    chain->module_count++;
    if (engine->num_controls > 0) {
        ctrl_err = register_controls(engine);
    }

    return ctrl_err;
}

void process_fx_chain(fx_chain* chain, const float* in_buf, float* out_buf, int buffer_size) {

    int m_num = chain->module_count, i = 0;
    //printf("processing audio\n");
    memcpy( out_buf, in_buf, buffer_size * sizeof(float) );
    scope_test_signal(out_buf, buffer_size);
    scope_write(SCOPE_CH_A, out_buf, buffer_size);       // dry input signal

    while (i < m_num) {

        chain->slots[i].process_func(chain->slots[i].engine, out_buf, out_buf, buffer_size);
        i++;
    }

    scope_write(SCOPE_CH_B, out_buf, buffer_size);      // wet output signal
    mono_to_stereo(out_buf, buffer_size);
}

//takes the output buffer in a mono state. Works back through the array, duplicating the mono input
void mono_to_stereo(float* out_buffer, int mono_size) {

    int stereo_length = mono_size * 2;
    int i = mono_size - 1, j = stereo_length - 1;       //set i to the index of the last mono sample in the buffer, set j to the last index of the buffer

    while (i >= 0) {

        out_buffer[j] = out_buffer[i];
        j--;
        out_buffer[j] = out_buffer[i];
        j--;
        i--;
    }
}