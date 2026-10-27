#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include <portaudio.h>
#include <pa_asio.h>
#include "pedal_processor.h"
#include "control_def.h"
#include "ui_listener.h"
#include "dsp_tools.h"
#include "master_effects.h"
#include "distortion_effects.h"
#include "EQ_effects.h"
#include "compressor_effects.h"
#include "distortion_exp.h"
#include "delay_effects.h"
#include "amp_model.h"

#define UPDATE_INTERVAL         6                   // The number of controls that are checked an possibly updated per callback cycle

//global arrays, to cache the current value of effects parameters, and the pointers needed to update them.
float current_params[MAX_CONTROLS];
float update_params[MAX_CONTROLS];
param_update update_funcs[MAX_CONTROLS];
effects_engine* e_points[MAX_CONTROLS];

// control_index holds the last index of current_params checked for a change from the UI.
// a loop in callback checks UPDATE_INTERVAL cached parameters against same index in update_params
int control_index = 0;

static int callback(const void *input, void *output, unsigned long frame_count,
                     const PaStreamCallbackTimeInfo *time_info,
                     PaStreamCallbackFlags status_flags, void *user_data) 
{
    //For measuring latency, see below
    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

	(void)time_info; (void)status_flags;

    // if (status_flags & (paInputUnderflow | paInputOverflow | paOutputUnderflow | paOutputOverflow)) {
    //     fprintf(stderr, "Glitch flags set! flags=0x%lx\n", (unsigned long)status_flags);
    // }

    //Read atomic variables - these values are set by UI components. 
    int ctrl_count = get_control_count();
    for (int i = 0; i < UPDATE_INTERVAL; i++) {

        update_params[control_index] = get_control(control_index);

        if (update_params[control_index] != current_params[control_index]) {

            param_update func = get_control_func(control_index);
            func(e_points[control_index], update_params[control_index]);
            current_params[control_index] = update_params[control_index];
        }

        if (control_index == ctrl_count - 1) {
            control_index = 0;
        }
        else {
            control_index++;
        }
    }
    
	//const float *in = (const float *)input;
    float* in = (float*)input;
    float* out = (float*)output;

    fx_chain* chain = (fx_chain*)user_data;

    if (in == NULL) {

			//fprintf(stderr, "silence detected.\n");
			unsigned long i = 0;
			while (i < frame_count){
				
				*out++ = SILENCE;
            	*out++ = SILENCE;
				i++;
			}
			return paContinue;
    }
    
    //process the entire input buffer
    //passthrough_buffer(FRAMES_PER_BUFFER, in, out);
    process_fx_chain(chain, in, out, FRAMES_PER_BUFFER);

    //Measures Latency
    QueryPerformanceCounter(&end);
    double ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
    double budget_ms = (double)frame_count / SAMPLE_RATE * 1000.0;

    static int print_counter = 0;
    static double worst_ms = 0.0;
    if (ms > worst_ms) worst_ms = ms;

    if (print_counter++ % 200 == 0) {
        fprintf(stderr, "callback took %.3f ms (worst so far: %.3f ms, budget: %.3f ms)\n",
                ms, worst_ms, budget_ms);
    }
	return paContinue;
}

static PaDeviceIndex find_device_by_name_and_api(const char *name_substring, int want_input, PaHostApiTypeId api_type) {

    int num_devices = Pa_GetDeviceCount();
    for (int i = 0; i < num_devices; i++) {
        
		const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
        if (!info) continue;

        const PaHostApiInfo *host = Pa_GetHostApiInfo(info->hostApi);
        if (!host || host->type != api_type) continue;
        if (want_input && info->maxInputChannels <= 0) continue;
        if (!want_input && info->maxOutputChannels <= 0) continue;
        if (strstr(info->name, name_substring) != NULL) return i;
    }
    return paNoDevice;
}

int main(void) {
	Pa_Initialize();
    //printf("PA init: %d\n", pa_init);

	/* Force ASIO specifically, rather than trusting the default device */
    PaDeviceIndex m2_in = find_device_by_name_and_api("MOTU M Series", 1, paASIO);
    PaDeviceIndex m2_out = find_device_by_name_and_api("MOTU M Series", 0, paASIO);

    if (m2_in == paNoDevice || m2_out == paNoDevice) {
        fprintf(stderr, "Could not find M2 under ASIO host API.\n");
        Pa_Terminate();
        return 1;
    }

    const PaDeviceInfo *in_info = Pa_GetDeviceInfo(m2_in);
    const PaDeviceInfo *out_info = Pa_GetDeviceInfo(m2_out);

    PaStreamParameters in_params = {
        .device = m2_in,
        .channelCount = NUM_IN_CHANNELS,
        .sampleFormat = paFloat32,
        .suggestedLatency = in_info->defaultLowInputLatency,
    };
    PaStreamParameters out_params = {
        .device = m2_out,
        .channelCount = NUM_OUT_CHANNELS,
        .sampleFormat = paFloat32,
        .suggestedLatency = out_info->defaultLowOutputLatency,
    };

    //Initialize effect or filter parameters
    int up_sampling_factor = 4;

    fx_chain chain;
    fx_chain_init(&chain);

    // Initialize model objects
    envelope_follower e_fol;
    envelope_follower_init(&e_fol, 10.0f, 50.0f, SAMPLE_RATE);
    compressor_engine c_engine;
    compressor_init(&c_engine, &e_fol, SAMPLE_RATE);
    distortion_engine d_engine;
    build_fav_distort_1(&d_engine, up_sampling_factor, SAMPLE_RATE, FRAMES_PER_BUFFER);
    //build_octave_fuzz(&d_engine, SAMPLE_RATE, FRAMES_PER_BUFFER);
    //build_octave_fuzz_v2(&d_engine, SAMPLE_RATE, FRAMES_PER_BUFFER);
    //stage_testing(&d_engine, SAMPLE_RATE, FRAMES_PER_BUFFER);
    EQ_engine eq_engine;
    EQ_init(&eq_engine, SAMPLE_RATE);
    delay_engine dl_engine;
    delay_engine_init(&dl_engine, SAMPLE_RATE, FRAMES_PER_BUFFER);

    amp_model a_mod;
    amp_model_init(&a_mod, SAMPLE_RATE, FRAMES_PER_BUFFER);

    master_effects_engine master_engine;
    master_effects_init(&master_engine, FRAMES_PER_BUFFER);


    // append the effects modules to the fx_chain
    // control registration is handled by fx_chain_append
    int append_ers[NUM_EFFECTS];
    // append_ers[0] = fx_chain_append(&chain, &c_engine.base, process_compressor);
    // append_ers[1] = fx_chain_append(&chain, &d_engine.base, process_distortion);
    // append_ers[2] = fx_chain_append(&chain, &eq_engine.base, process_EQ);
    append_ers[3] = fx_chain_append(&chain, &a_mod.base, process_amp_model);
    // append_ers[4] = fx_chain_append(&chain, &dl_engine.base, process_delay);
    append_ers[5] = fx_chain_append(&chain, &master_engine.base, process_master_volume);
    
    void* chain_ptr = &chain;

    for (int i = 0, j = 0; i < chain.module_count; i++) {
        j += append_ers[i];
        if (j > 0) {
            fprintf(stderr, "Error building fx_chain, at index: %d\n", i);
        }
    }
   
    //Set global caching parameters
    int ctrl_count = get_control_count();                  // Once controls are registered, the shared Params will have been intialized with their default values
    for (int i = 0; i < ctrl_count; i++) {                 // so we set the current values from those 

        current_params[i] = get_control(i);
    }

    // Next we initialize the listener thread. This thread listens from control signals from the UI components
    // and update global effects parameters with atomic writes
    if (start_control_listener() != 0) {
        fprintf(stderr, "Could not start the control listener (port 9001 in use?)\n");
        return 1;
    }

    PaStream *stream;
    // fprintf(stderr, "m2_in=%d m2_out=%d in_params.device=%d out_params.device=%d\n",
    //     m2_in, m2_out, in_params.device, out_params.device);
    PaError err = Pa_OpenStream(&stream, &in_params, &out_params, SAMPLE_RATE, FRAMES_PER_BUFFER, paClipOff, callback, chain_ptr);

    if (err != paNoError) {
        fprintf(stderr, "Pa_OpenStream failed: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        return 1;
    }

    Pa_StartStream(stream);

    const PaStreamInfo *stream_info = Pa_GetStreamInfo(stream);
    printf("Using ASIO. Actual input latency: %.4f s, output latency: %.4f s\n", stream_info->inputLatency, stream_info->outputLatency);
    printf("Passing audio straight through. Press Enter to stop.\n");
    getchar();

    //stop UI listener thread
    stop_control_listener();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    
    return 0;
}

void fx_chain_init(fx_chain* chain) {

    chain->module_count = 0;
}

int fx_chain_append(fx_chain* chain, effects_engine* engine, prcs_function process_func) {

    if(chain->module_count >= NUM_EFFECTS || engine == NULL || process_func == NULL) {
        return 1;
    }

    chain->slots[chain->module_count].engine = engine;
    chain->slots[chain->module_count].process_func = process_func;
    chain->module_count++;
    register_controls(engine);

    return 0;
}

void process_fx_chain(fx_chain* chain, float* in_buf, float* out_buf, int buffer_size) {

    int m_num = chain->module_count, i = 0;
    //printf("processing audio\n");
    chain->slots[i].process_func(chain->slots[i].engine, in_buf, out_buf, buffer_size);
    i++;
    while (i < m_num) {
        
        chain->slots[i].process_func(chain->slots[i].engine, out_buf, out_buf, buffer_size);
        i++;
    }

    mono_to_stereo(out_buf, buffer_size);
}

float passthrough(float signal) {

    return signal;
}

void passthrough_buffer(int buf_size, float* in_buf, float* out_buf) {

    int i = 0;
    float mono_output;

    while (i < buf_size) {

        mono_output = in_buf[i];
        *out_buf++ = mono_output;
        *out_buf++ = mono_output;
        i++;
    }
}

//takes the output buffer in a mono state. Works back through the array, duplicating the mono input
void mono_to_stereo(float* out_buffer, int mono_size) {

    int stereo_length = mono_size * 2;
    int i = mono_size - 1, j = stereo_length - 1;       //set i to the index of the last mono sample in the buffer, set j to the last index of the buffer

    while (i > 0) {

        out_buffer[j] = out_buffer[i];
        j--;
        out_buffer[j] = out_buffer[i];
        j--; 
        i--;
    }
}

/*
Dead simple distortion effect. Takes a drive value, and a sample. 
Then it adds gain, and clips it without elegance
*/
float basic_clipping(float drive, float signal) {

    float clipped = signal;
    clipped *= drive;

    if (clipped < -1) return -1;
    if (clipped > 1) return 1;

    //if drive is <= 1, then there is nothing to clip
    return signal;
}

float simple_soft_clipping(float drive, float signal) {

    float driven = signal * drive;

    return tanhf(driven);
}
