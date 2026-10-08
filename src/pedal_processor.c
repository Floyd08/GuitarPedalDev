#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include <windows.h>
#include <portaudio.h>
#include <pa_asio.h>
#include "pedal_processor.h"
#include "ui_listener.h"
#include "distortion_effects.h"
#include "EQ_effects.h"
#include "distortion_exp.h"

//global variables, to cache the current value of effects parameters

float current_volume;
float current_gain;
float current_bias;
float current_bass;
float current_mid;
float current_treble;
float current_bass_freq;
float current_mid_freq;
float current_treble_freq;

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

    //Read atomic variables - these values are set by UI components
    //float m_volume = atomic_load_explicit(&master_volume, memory_order_relaxed);

    float m_volume = get_volume();
    float m_gain = get_gain();
    float m_bias = get_bias();
    float m_bass = get_bass_gain();
    float m_mid = get_mid_gain();
    float m_treble = get_treble_gain();
    float m_bass_freq = get_bass_freq();
    float m_mid_freq = get_mid_freq();
    float m_treble_freq = get_treble_freq();

	//const float *in = (const float *)input;
    float* in = (float*)input;
    float* out = (float*)output;

    //cast user data to an arrray of objects so I can cast them to the needed type below
    void** model_array = (void**)user_data;

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
    
    if (current_volume != m_volume) {
        current_volume = m_volume;
    }
    if (current_gain != m_gain) {
        set_distort_gain((distortion_engine*)model_array[0], m_gain);
        current_gain = m_gain;
        printf("gain updated to: %f\n", current_gain);
    }
    else if (current_bias != m_bias) {
        set_distort_bias((distortion_engine*)model_array[0], m_bias);
        current_bias = m_bias;
        printf("bias updated to: %f\n", current_bias);
    }
    else if (current_bass != m_bass) {
        EQ_update_filter_gain( (EQ_engine*)model_array[1], m_bass, LOWSHELF);
        current_bass = m_bass;
        printf("bass gain updated to: %f\n", current_bass);
    }
    else if (current_mid != m_mid) {
        EQ_update_filter_gain((EQ_engine*)model_array[1], m_mid, PEAK);
        current_mid = m_mid;
        printf("mid gain updated to: %f\n", current_mid);
    }
    else if (current_treble != m_treble) {
        EQ_update_filter_gain((EQ_engine*)model_array[1], m_treble, HIGHSHELF);
        current_treble = m_treble;
        printf("treble gain updated to: %f\n", current_treble);
    }
    else if (current_bass_freq != m_bass_freq) {
        EQ_update_filter_centre((EQ_engine*)model_array[1], m_bass_freq, LOWSHELF);
        current_bass_freq = m_bass_freq;
        printf("bass freq updated to: %f\n", m_bass_freq);
    }
    else if (current_mid_freq != m_mid_freq) {
        EQ_update_filter_centre((EQ_engine*)model_array[1], m_mid_freq, PEAK);
        current_mid_freq = m_mid_freq;
        printf("mid freq updated to: %f\n", m_mid_freq);
    }
    else if (current_treble_freq != m_treble_freq) {
        EQ_update_filter_centre((EQ_engine*)model_array[1], m_mid_freq, HIGHSHELF);
        current_treble_freq = m_treble_freq;
        printf("treble freq updated to: %f\n", m_treble_freq);
    }

    //process the entire input buffer
    //passthrough_buffer(FRAMES_PER_BUFFER, in, out);
    //process_distortion_exp1((exp_distortion_engine*)model_array[0], in, out);
    process_distortion( (distortion_engine*)model_array[0], in, out );
    process_EQ( (EQ_engine*)model_array[1], out, out, FRAMES_PER_BUFFER);

    //set master volume and process into stereo
    set_volume(current_volume, out, FRAMES_PER_BUFFER);
    mono_to_stereo(out, FRAMES_PER_BUFFER);

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
    int up_sampling_factor = 1;
    float upsampling_buffer[FRAMES_PER_BUFFER * up_sampling_factor];             //If the input were not Mono, we'd need to take the frame size into account here as well

    //Initialize model objects
    // exp_distortion_init(&d_engine, d_effect, 5, up_sampling_factor, SAMPLE_RATE, FRAMES_PER_BUFFER, upsampling_buffer);
    distortion_engine d_engine;
    build_fav_distort_1(&d_engine, up_sampling_factor, SAMPLE_RATE, FRAMES_PER_BUFFER, upsampling_buffer);
    EQ_engine eq_engine;
    EQ_init(&eq_engine, SAMPLE_RATE);

    //build an array of pointers to model objects, to pass in with user_data
    //the array MUST be in the order in which the signal is meant to be processed
    void* user_data[NUM_EFFECTS];
    user_data[0] = &d_engine;
    user_data[1] = &eq_engine;

    //Set global caching parameters
    current_volume = 0.5;
    current_gain = 0.5;
    current_bias = 0.5;
    current_bass = 0.5;
    current_mid = 0.5;
    current_treble = 0.5;
    current_mid_freq = 0.5;
   
    /*
    *   Next we initialize the listener thread. This thread listens from control signals from the UI components
    *   and update global effects parameters with atomic writes
    */

    if (start_control_listener() != 0) {
        fprintf(stderr, "Could not start the control listener (port 9001 in use?)\n");
        return 1;
    }

    PaStream *stream;
    PaError err = Pa_OpenStream(&stream, &in_params, &out_params, SAMPLE_RATE, FRAMES_PER_BUFFER, paClipOff, callback, user_data);

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

    //free effect models here
    distortion_free(&d_engine);
    EQ_free(&eq_engine);

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    
    return 0;
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

void set_volume(float volumn_factor, float *out_buf, int buffer_size) {

    int i = 0;
    while (i < buffer_size) {
        out_buf[i] = out_buf[i] * volumn_factor;
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
