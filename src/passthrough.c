/*
 * passthrough.c
 *
 * The simplest possible PortAudio program: send live input straight to
 * output, unmodified. Good starting point before adding any DSP.
 *
 * Build:
 *   gcc -O2 -o passthrough passthrough.c -lportaudio
 *
 * Run:
 *   ./passthrough
 *
 * WARNING: input goes straight to output - keep volume low to start,
 * especially if input and output share a device (feedback risk).
 */

#include <stdio.h>
#include <string.h>
#include <portaudio.h>

#define SAMPLE_RATE       48000
#define FRAMES_PER_BUFFER 128
#define NUM_CHANNELS      1

static int callback(const void *input, void *output, unsigned long frame_count,
                     const PaStreamCallbackTimeInfo *time_info,
                     PaStreamCallbackFlags status_flags, void *user_data)
{
    (void)time_info; (void)status_flags; (void)user_data;
    const float *in = (const float *)input;
    float *out = (float *)output;
    for (unsigned long i = 0; i < frame_count; i++) {
        out[i] = (in != NULL) ? in[i] : 0.0f;
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
        fprintf(stderr, "Could not find M2 under ASIO host API. "
                        "Is the ASIO driver installed and PortAudio built with ASIO support?\n");
        Pa_Terminate();
        return 1;
    }

    const PaDeviceInfo *in_info = Pa_GetDeviceInfo(m2_in);
    const PaDeviceInfo *out_info = Pa_GetDeviceInfo(m2_out);

    PaStreamParameters in_params = {
        .device = m2_in,
        .channelCount = NUM_CHANNELS,
        .sampleFormat = paFloat32,
        .suggestedLatency = in_info->defaultLowInputLatency,
    };
    PaStreamParameters out_params = {
        .device = m2_out,
        .channelCount = NUM_CHANNELS,
        .sampleFormat = paFloat32,
        .suggestedLatency = out_info->defaultLowOutputLatency,
    };

    PaStream *stream;
    PaError err = Pa_OpenStream(&stream, &in_params, &out_params, SAMPLE_RATE,
                                 FRAMES_PER_BUFFER, paClipOff, callback, NULL);
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

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}