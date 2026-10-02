/*
 * portaudio_knobs_demo.c
 *
 * A minimal PortAudio passthrough application: audio in -> (gain *
 * volume) -> audio out, with the multiplier controlled live by the
 * Gain and Volume knobs from audio_knobs.c/.h.
 *
 * IMPORTANT — thread safety:
 *   PortAudio calls audioCallback() on its own dedicated, real-time
 *   audio thread, completely separate from the SDL main loop below.
 *   The callback must never call SDL functions, allocate memory, lock
 *   a mutex, or do anything that could block — any of that risks
 *   audio glitches/dropouts. The only thing shared between the two
 *   threads is a pair of C11 atomics (g_gain, g_volume): the SDL loop
 *   writes to them after reading the knobs each frame, and the audio
 *   callback reads them each buffer. Atomic float load/store is
 *   lock-free on every platform this is likely to run on, so this is
 *   safe and cheap enough for the audio thread.
 *
 * Dependencies: SDL2, SDL2_ttf, PortAudio (v19 API).
 *
 * Build (Linux/macOS):
 *   gcc audio_knobs.c portaudio_knobs_demo.c -o portaudio_knobs_demo \
 *       `pkg-config --cflags --libs sdl2 SDL2_ttf portaudio-2.0` -lm
 *
 * Build (Windows, MSYS2/MinGW, via the Makefile):
 *   mingw32-make
 *
 * MSYS2 package for PortAudio:
 *   pacman -S mingw-w64-x86_64-portaudio
 */

#include "audio_knobs.h"

#include <portaudio.h>

#include <stdatomic.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define WINDOW_W 480
#define WINDOW_H 300
#define SAMPLE_RATE 44100.0
#define FRAMES_PER_BUFFER 256
#define NUM_CHANNELS 2 /* stereo in, stereo out */

/* Shared between the SDL thread (writer) and the PortAudio callback
 * thread (reader). Lock-free atomic float — see thread-safety note
 * above. Initial values match the knobs' starting values below. */
static _Atomic float g_gain = 1.0f;
static _Atomic float g_volume = 0.75f;

/* --- audio callback (runs on PortAudio's real-time thread) ------------- */

static int audioCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData) {
    (void)timeInfo;
    (void)statusFlags;
    (void)userData;

    const float *in = (const float *)inputBuffer;
    float *out = (float *)outputBuffer;
    unsigned long sampleCount = framesPerBuffer * NUM_CHANNELS;

    /* No new memory, no locks, no SDL calls here — just arithmetic. */
    float gain = atomic_load_explicit(&g_gain, memory_order_relaxed);
    float volume = atomic_load_explicit(&g_volume, memory_order_relaxed);
    float amp = gain * volume;

    if (in == NULL) {
        /* Input underflowed this buffer; output silence rather than
         * garbage or stale data. */
        for (unsigned long i = 0; i < sampleCount; i++) {
            out[i] = 0.0f;
        }
        return paContinue;
    }

    for (unsigned long i = 0; i < sampleCount; i++) {
        out[i] = in[i] * amp;
    }

    return paContinue;
}

/* --- app entry point (SDL main thread) ---------------------------------- */

static int app_main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    PaError paErr = Pa_Initialize();
    if (paErr != paNoError) {
        fprintf(stderr, "Pa_Initialize failed: %s\n", Pa_GetErrorText(paErr));
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "My Port Audio - Gain / Volume",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        Pa_Terminate();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        Pa_Terminate();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    TTF_Font *font = load_app_font();
    if (!font) {
        fprintf(stderr,
                "Warning: no font found; knob labels won't be drawn. "
                "Place a .ttf file named 'font.ttf' next to the executable.\n");
    }

    Knob gainKnob = {
        .label = "Gain", .cx = WINDOW_W / 2 - 100, .cy = WINDOW_H / 2,
        .radius = KNOB_RADIUS, .min = 0.0f, .max = 2.0f, .value = 1.0f
    };
    Knob volumeKnob = {
        .label = "Volume", .cx = WINDOW_W / 2 + 100, .cy = WINDOW_H / 2,
        .radius = KNOB_RADIUS, .min = 0.0f, .max = 1.0f, .value = 0.75f
    };

    if (font) {
        SDL_Color white = {255, 255, 255, 255};
        gainKnob.label_texture = create_label_texture(
            renderer, font, gainKnob.label, white,
            &gainKnob.label_w, &gainKnob.label_h);
        volumeKnob.label_texture = create_label_texture(
            renderer, font, volumeKnob.label, white,
            &volumeKnob.label_w, &volumeKnob.label_h);
    }

    /* Open a full-duplex stream on the default input/output devices,
     * stereo float32 in and out — the simplest format PortAudio and
     * the callback above both agree on without any conversion code. */
    PaStream *stream = NULL;
    paErr = Pa_OpenDefaultStream(
        &stream,
        NUM_CHANNELS,        /* input channels */
        NUM_CHANNELS,        /* output channels */
        paFloat32,            /* sample format */
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        audioCallback,
        NULL /* no userData; we use the file-scope atomics instead */
    );
    if (paErr != paNoError) {
        fprintf(stderr, "Pa_OpenDefaultStream failed: %s\n", Pa_GetErrorText(paErr));
        goto cleanup;
    }

    paErr = Pa_StartStream(stream);
    if (paErr != paNoError) {
        fprintf(stderr, "Pa_StartStream failed: %s\n", Pa_GetErrorText(paErr));
        goto cleanup;
    }

    bool running = true;
    SDL_Event e;
    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            handle_knob_event(&gainKnob, &e);
            handle_knob_event(&volumeKnob, &e);
        }

        /* Publish the latest knob values to the audio thread. Plain
         * atomic stores — cheap enough to do unconditionally every
         * frame rather than only on change. */
        atomic_store_explicit(&g_gain, gainKnob.value, memory_order_relaxed);
        atomic_store_explicit(&g_volume, volumeKnob.value, memory_order_relaxed);

        SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
        SDL_RenderClear(renderer);

        draw_knob(renderer, &gainKnob);
        draw_knob(renderer, &volumeKnob);

        SDL_RenderPresent(renderer);
        SDL_Delay(16); /* ~60 fps */
    }

    Pa_StopStream(stream);

cleanup:
    if (stream) Pa_CloseStream(stream);
    if (gainKnob.label_texture) SDL_DestroyTexture(gainKnob.label_texture);
    if (volumeKnob.label_texture) SDL_DestroyTexture(volumeKnob.label_texture);
    if (font) TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    Pa_Terminate();
    TTF_Quit();
    SDL_Quit();
    return 0;
}

/* --- dual entry points ---------------------------------------------------
 * See the note in audio_knobs.c's history: some MinGW/MSYS2 toolchain
 * setups end up expecting WinMain() instead of (or as well as) main(),
 * depending on how the default subsystem resolves at link time.
 * Providing both means this links correctly either way. __argc/__argv
 * are populated by the MinGW runtime before either entry point runs. */

int main(int argc, char *argv[]) {
    return app_main(argc, argv);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return app_main(__argc, __argv);
}
#endif
