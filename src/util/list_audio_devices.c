#include <stdio.h>
#include "portaudio.h"

static void print_device_info(PaDeviceIndex index, const PaDeviceInfo *info) {
    const PaHostApiInfo *host_api_info = Pa_GetHostApiInfo(info->hostApi);
    PaDeviceIndex default_input = Pa_GetDefaultInputDevice();
    PaDeviceIndex default_output = Pa_GetDefaultOutputDevice();

    printf("Device #%d: %s\n", index, info->name);
    printf("  Host API:             %s\n", host_api_info ? host_api_info->name : "Unknown");
    printf("  Max input channels:   %d\n", info->maxInputChannels);
    printf("  Max output channels:  %d\n", info->maxOutputChannels);
    printf("  Default sample rate:  %.0f Hz\n", info->defaultSampleRate);
    printf("  Default low latency:  in=%.4fs  out=%.4fs\n",
           info->defaultLowInputLatency, info->defaultLowOutputLatency);
    printf("  Default high latency: in=%.4fs  out=%.4fs\n",
           info->defaultHighInputLatency, info->defaultHighOutputLatency);

    if (index == default_input) {
        printf("  --> This is the default INPUT device\n");
    }
    if (index == default_output) {
        printf("  --> This is the default OUTPUT device\n");
    }

    printf("\n");
}

int main(void) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "Pa_Initialize failed: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    int num_devices = Pa_GetDeviceCount();
    if (num_devices < 0) {
        fprintf(stderr, "Pa_GetDeviceCount failed: %s\n", Pa_GetErrorText(num_devices));
        Pa_Terminate();
        return 1;
    }

    if (num_devices == 0) {
        printf("No audio devices found.\n");
    } else {
        printf("Found %d audio device(s):\n\n", num_devices);
        for (PaDeviceIndex i = 0; i < num_devices; i++) {
            const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
            if (info != NULL) {
                print_device_info(i, info);
            } else {
                printf("Device #%d: (could not retrieve info)\n\n", i);
            }
        }
    }

    err = Pa_Terminate();
    if (err != paNoError) {
        fprintf(stderr, "Pa_Terminate failed: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    return 0;
}