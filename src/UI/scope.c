/*
 * scope.c
 *
 * ASIO passthrough (MOTU M Series) + a live scrolling oscilloscope built
 * with SDL2. The PortAudio callback writes samples into a lock-free ring
 * buffer; the main thread's render loop reads from that ring buffer and
 * draws a waveform, completely separate from the real-time audio path.
 *
 * Build (MinGW example - adjust paths to match your project):
 *   gcc -O2 scope.c pa_ringbuffer.c -o scope.exe \
 *       -Iextensions/portaudio/include -Lextensions/portaudio/build \
 *       -lportaudio -lwinmm -lole32 -luuid -lsetupapi \
 *       $(pkg-config --cflags --libs sdl2)
 *
 * Note: pa_ringbuffer.c / pa_ringbuffer.h ship with the PortAudio source
 * tree under portaudio/src/common/ - you'll need to add that path to
 * your -I flags and compile pa_ringbuffer.c alongside this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <portaudio.h>
#include <pa_ringbuffer.h>
#include <SDL2/SDL.h>

#define SAMPLE_RATE        48000
#define FRAMES_PER_BUFFER  128
#define NUM_CHANNELS       1

#define RING_BUFFER_FRAMES 16384   /* must be a power of 2 */

#define WINDOW_WIDTH       800
#define WINDOW_HEIGHT      400
#define DISPLAY_SAMPLES    800     /* one sample plotted per pixel column */

typedef struct {
    PaUtilRingBuffer ring_buffer;
    float *ring_buffer_data;
} AppState;

/* --- Audio callback: unchanged passthrough, plus a ring buffer write --- */
static int callback(const void *input, void *output, unsigned long frame_count,
                     const PaStreamCallbackTimeInfo *time_info,
                     PaStreamCallbackFlags status_flags, void *user_data)
{
    (void)time_info; (void)status_flags;
    AppState *state = (AppState *)user_data;

    const float *in = (const float *)input;
    float *out = (float *)output;

    for (unsigned long i = 0; i < frame_count; i++) {
        out[i] = (in != NULL) ? in[i] : 0.0f;
    }

    /* Non-blocking write; if the visualizer thread is behind and the
     * buffer is full, excess samples are simply dropped. The audio
     * path above is completely unaffected either way. */
    PaUtil_WriteRingBuffer(&state->ring_buffer, out, frame_count);

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

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;  // unused, but SDL2 requires this signature
    
    /* ---------- PortAudio setup (same as passthrough.c) ---------- */
    Pa_Initialize();

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

    /* ---------- Ring buffer setup ---------- */
    AppState state = {0};
    state.ring_buffer_data = malloc(RING_BUFFER_FRAMES * sizeof(float));
    if (!state.ring_buffer_data) {
        fprintf(stderr, "Failed to allocate ring buffer memory\n");
        Pa_Terminate();
        return 1;
    }
    PaUtil_InitializeRingBuffer(&state.ring_buffer, sizeof(float),
                                 RING_BUFFER_FRAMES, state.ring_buffer_data);

    PaStream *stream;
    PaError err = Pa_OpenStream(&stream, &in_params, &out_params, SAMPLE_RATE,
                                 FRAMES_PER_BUFFER, paClipOff, callback, &state);
    if (err != paNoError) {
        fprintf(stderr, "Pa_OpenStream failed: %s\n", Pa_GetErrorText(err));
        free(state.ring_buffer_data);
        Pa_Terminate();
        return 1;
    }
    Pa_StartStream(stream);

    const PaStreamInfo *stream_info = Pa_GetStreamInfo(stream);
    printf("Using ASIO. Input latency: %.4fs, output latency: %.4fs\n",
           stream_info->inputLatency, stream_info->outputLatency);

    /* ---------- SDL2 setup ---------- */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        free(state.ring_buffer_data);
        Pa_Terminate();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "Audio Scope",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );
    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    /* Holds the most recent samples we've pulled from the ring buffer,
     * used to draw one full frame of the waveform. This lives on the
     * main thread only - the audio callback never touches it. */
    float display_buffer[DISPLAY_SAMPLES];
    memset(display_buffer, 0, sizeof(display_buffer));

    printf("Scope running. Close the window to stop.\n");

    int running = 1;
    while (running) {
        /* --- Handle window events (close button, etc.) --- */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        /* --- Pull whatever new samples are available --- */
        /* Shift the old contents left and append new samples at the end,
         * so the display always shows the most recent DISPLAY_SAMPLES
         * regardless of how many arrived since the last frame. */
        float new_samples[DISPLAY_SAMPLES];
        ring_buffer_size_t available = PaUtil_GetRingBufferReadAvailable(&state.ring_buffer);
        ring_buffer_size_t to_read = (available > DISPLAY_SAMPLES) ? DISPLAY_SAMPLES : available;
        ring_buffer_size_t frames_read =
            PaUtil_ReadRingBuffer(&state.ring_buffer, new_samples, to_read);

        if (frames_read > 0) {
            /* Discard the oldest (frames_read) samples, shift the rest down */
            memmove(display_buffer, display_buffer + frames_read,
                    (DISPLAY_SAMPLES - frames_read) * sizeof(float));
            /* Append the newly read samples at the end */
            memcpy(display_buffer + (DISPLAY_SAMPLES - frames_read), new_samples,
                   frames_read * sizeof(float));
        }

        /* --- Draw --- */
        SDL_SetRenderDrawColor(renderer, 10, 10, 10, 255);  /* dark background */
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);  /* center line */
        SDL_RenderDrawLine(renderer, 0, WINDOW_HEIGHT / 2, WINDOW_WIDTH, WINDOW_HEIGHT / 2);

        SDL_SetRenderDrawColor(renderer, 0, 255, 120, 255); /* waveform color */
        for (int x = 0; x < DISPLAY_SAMPLES - 1 && x < WINDOW_WIDTH - 1; x++) {
            /* Map each sample from [-1.0, 1.0] to a pixel row, with 0.0
             * landing on the vertical center of the window. */
            int y1 = (int)(WINDOW_HEIGHT / 2 - display_buffer[x] * (WINDOW_HEIGHT / 2));
            int y2 = (int)(WINDOW_HEIGHT / 2 - display_buffer[x + 1] * (WINDOW_HEIGHT / 2));
            SDL_RenderDrawLine(renderer, x, y1, x + 1, y2);
        }

        SDL_RenderPresent(renderer);
    }

    /* ---------- Cleanup ---------- */
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    Pa_StopStream(stream);
    Pa_CloseStream(stream);
    free(state.ring_buffer_data);
    Pa_Terminate();

    return 0;
}
