/*
 * main_tube_preamp.c
 *
 * Live version: guitar in -> tube preamp emulation -> speakers/headphones,
 * reusing the same audio_io module from the compressor example (audio_io.c
 * is generic - it just calls compressor_process() by name though, so this
 * file talks to PortAudio directly rather than reusing audio_io.c as-is;
 * see the note below).
 *
 * Build:
 *   gcc -O2 -o tube_preamp_live main_tube_preamp.c tube_preamp.c -lportaudio -lm
 *
 * Run:
 *   ./tube_preamp_live
 *
 * WARNING: live passthrough - keep volume low to start.
 */

#include <stdio.h>
#include <portaudio.h>
#include "tube_preamp.h"

#define SAMPLE_RATE       48000
#define FRAMES_PER_BUFFER 256
#define NUM_CHANNELS      1

static int callback(const void *input, void *output, unsigned long frame_count,
                     const PaStreamCallbackTimeInfo *time_info,
                     PaStreamCallbackFlags status_flags, void *user_data) {
    (void)time_info; (void)status_flags;

    TubePreamp *preamp = (TubePreamp *)user_data;
    const float *in = (const float *)input;
    float *out = (float *)output;

    for (unsigned long i = 0; i < frame_count; i++) {
        out[i] = (in != NULL) ? tube_preamp_process(preamp, in[i]) : 0.0f;
    }
    return paContinue;
}

int main(void) {
    TubePreamp preamp;
    tube_preamp_init(&preamp,
                      /*drive=*/        4.0,
                      /*asymmetry=*/    0.3,
                      /*lowpass_hz=*/   7000.0,
                      /*highpass_hz=*/  80.0,
                      /*output_level=*/ 0.8,
                      SAMPLE_RATE);

    Pa_Initialize();

    PaStreamParameters in_params = {
        .device = Pa_GetDefaultInputDevice(),
        .channelCount = NUM_CHANNELS,
        .sampleFormat = paFloat32,
        .suggestedLatency = Pa_GetDeviceInfo(Pa_GetDefaultInputDevice())->defaultLowInputLatency,
    };
    PaStreamParameters out_params = {
        .device = Pa_GetDefaultOutputDevice(),
        .channelCount = NUM_CHANNELS,
        .sampleFormat = paFloat32,
        .suggestedLatency = Pa_GetDeviceInfo(Pa_GetDefaultOutputDevice())->defaultLowOutputLatency,
    };

    PaStream *stream;
    Pa_OpenStream(&stream, &in_params, &out_params, SAMPLE_RATE,
                  FRAMES_PER_BUFFER, paClipOff, callback, &preamp);
    Pa_StartStream(stream);

    printf("Tube preamp live: drive=4.0  asymmetry=0.3  lpf=7kHz  hpf=80Hz\n");
    printf("Play your guitar. Press Enter to stop.\n");
    getchar();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    Pa_Terminate();
    return 0;
}
