#include <stdio.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include <portaudio.h>
#include <pa_asio.h>
#include "pedal_processor.h"
#include "distortion_effects.h"
#include "distortion_exp.h"

float mono_output;                                      //to hold processed output before it is duplicated for stereo output

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

	//const float *in = (const float *)input;
    float* in = (float*)input;
    float* out = (float*)output;

    //cast user data to an arrray of objects so I can cast them to the needed type below
    void** model_array = (void**)user_data;

    //Could this check be causing the static?
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
    //process_distortion_exp1((exp_distortion_engine*)model_array[0], in, out);
    process_distortion( (distortion_engine*)model_array[0], in, out );

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

static PaDeviceIndex find_device_by_name_and_api(const char *name_substring,
                                                    int want_input,
                                                    PaHostApiTypeId api_type) {
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
    
    /*
    Until I have a better idea, any initialization needed for effects or Amp modeling will happen below this comment
    So that any needed pointers can be passed into the callback function when the stream is opened
    */

    //Initialize effect or filter parameters
    int up_sampling_factor = 2;
    float upsampling_buffer[FRAMES_PER_BUFFER * up_sampling_factor];             //If the input were not Mono, we'd need to take the frame size into account here as well

    //Initialize model objects
    // exp_distortion_init(&d_engine, d_effect, 5, up_sampling_factor, SAMPLE_RATE, FRAMES_PER_BUFFER, upsampling_buffer);
    distortion_engine d_engine;
    build_fav_distort_1(&d_engine, up_sampling_factor, SAMPLE_RATE, FRAMES_PER_BUFFER, upsampling_buffer);

    //build an array of pointers to model objects, to pass in with user_data
    //the array MUST be in the order in which the signal is meant to be processed
    void* user_data[NUM_EFFECTS];
    user_data[0] = &d_engine;
   


    PaStream *stream;
    PaError err = Pa_OpenStream(&stream, &in_params, &out_params, SAMPLE_RATE,
                                    FRAMES_PER_BUFFER, paClipOff, callback, user_data);
    if (err != paNoError) {
        fprintf(stderr, "Pa_OpenStream failed: %s\n", Pa_GetErrorText(err));
        Pa_Terminate();
        return 1;
    }

    Pa_StartStream(stream);

    const PaStreamInfo *stream_info = Pa_GetStreamInfo(stream);
    printf("Using ASIO. Actual input latency: %.4f s, output latency: %.4f s\n",
           stream_info->inputLatency, stream_info->outputLatency);
    printf("Passing audio straight through. Press Enter to stop.\n");
    getchar();

    //free effect models here
    //distortion_free(&d_engine);

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    
    return 0;
}

/* 
Functions developed below are either experimental, or WIP.
any finished functions should be moved to a separate source file
the only exception are those few utilities present for testing purposes
*/

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

// A space for stock distortion configs
void build_fav_distort_1(distortion_engine* d_engine, int up_factor, int sample_rate, int buffer_size, float* up_buf) {

    // initialize distortion stages
	static distortion_stage d_stage1;
    static distortion_stage d_stage2;

    //initialize filters
    static biquad* hp1;
    hp1 = bq_new(HIGHPASS, 160, 0.7071, 0, sample_rate);
    static biquad* hp2;
    hp2 = bq_new(HIGHPASS, 80, 0.7071, 0, sample_rate);
	static biquad* pk1;
    pk1 = bq_new(PEAK, 1500, 0.7071, 2.5119, sample_rate);
    static biquad* pk2;
    pk2 = bq_new(PEAK, 1000, 1, 2.5119, sample_rate);
    static biquad* lp1;
    lp1 = bq_new(LOWPASS, 3000, 0.7071, 0, sample_rate);
    static biquad* lp2;
    lp2 = bq_new(LOWPASS, 6000, 0.7071, 0, sample_rate);

    //stack filters
    static biquad* pre_filters1[3];
    pre_filters1[0] = hp1;
    pre_filters1[1] = lp1;
    pre_filters1[2] = pk1;
    distortion_stage_init(&d_stage1, pre_filters1, 3, 0.0f, 1.0f, tanh_distortion, NULL, 0);

    static biquad* pre_filters2[3];
    pre_filters2[0] = hp2;
    pre_filters2[1] = lp2;
    pre_filters2[2] = pk2;
    distortion_stage_init(&d_stage2, pre_filters2, 3, 0.25f, 5.0f, tanh_distortion, NULL, 0);

    //stack distortion stages
    static distortion_stage* d_stages[2];
    d_stages[0] = &d_stage1;
    d_stages[1] = &d_stage2;

    distortion_init(d_engine, d_stages, 2, up_factor, sample_rate, buffer_size, up_buf);
}


//code saved below is for reference purposes, or will be added back above later

// AC_normal_channel norm_chan;
    // normal_channel_init(&norm_chan, SAMPLE_RATE);
    // AC_poweramp pow_amp;
    // poweramp_init(&pow_amp, SAMPLE_RATE);
    // cabinet_model cab_mod;
    // cabinet_model_init(&cab_mod);
    // cabinet_model_loadIR(&cab_mod, vox_2x12_ir_data, VOX_IR_LENGTH);


//void* user_data[NUM_EFFECTS];
    //user_data[0] = &norm_chan;
    //user_data[1] = &pow_amp;
    //user_data[2] = &cab_mod;
