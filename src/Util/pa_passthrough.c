/*
 * pa_passthrough.c
 *
 * Captures audio with PortAudio, runs it through a passthrough function,
 * plays it back out, and also streams the processed output as raw float32
 * samples over UDP to 127.0.0.1:UDP_PORT so another program can capture it.
 *
 * The audio callback never touches the network. It pushes samples into a
 * lock-free ring buffer, and a separate thread drains the ring and sends.
 *
 * Build (Linux/macOS):  gcc -O2 -std=c11 pa_passthrough.c -o pa_passthrough -lportaudio -lpthread
 * Build (MinGW):        gcc -O2 -std=c11 pa_passthrough.c -o pa_passthrough.exe -lportaudio -lws2_32
 * Build (MSVC):         cl /O2 /std:c11 /experimental:c11atomics pa_passthrough.c portaudio.lib ws2_32.lib
 *
 * Usage:
 *   pa_passthrough --list            list devices (shows host API, e.g. ASIO)
 *   pa_passthrough                   use default input and output devices
 *   pa_passthrough <in> <out>        use specific device indices
 *
 * Note: with ASIO, input and output must be the same device.
 */

#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L   /* for nanosleep() under -std=c11 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "portaudio.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  typedef SOCKET sock_t;
  #define SLEEP_MS(ms) Sleep(ms)
  #define CLOSE_SOCK   closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <pthread.h>
  typedef int sock_t;
  #define INVALID_SOCKET (-1)
  #include <time.h>
  static void sleep_ms(long ms)
  {
      struct timespec ts = { 0, ms * 1000000L };
      nanosleep(&ts, NULL);
  }
  #define SLEEP_MS(ms)   sleep_ms(ms)
  #define CLOSE_SOCK     close
#endif

/* ---- Configuration ---- */
#define SAMPLE_RATE      48000
#define FRAMES_PER_BUF   256
#define CHANNELS         1                 /* mono keeps the UDP stream simple */
#define UDP_PORT         9000
#define PACKET_SAMPLES   256               /* floats per UDP packet (1024 bytes) */
#define RING_SIZE        (1 << 16)         /* floats; must be a power of two */

/* ---- Lock-free single-producer/single-consumer ring buffer ---- */
static float         ring[RING_SIZE];
static atomic_size_t ring_head;            /* written by audio callback */
static atomic_size_t ring_tail;            /* written by sender thread  */
static atomic_ulong  dropped_samples;
static atomic_int    running = 1;

static void ring_push(const float *src, size_t n)
{
    size_t h = atomic_load_explicit(&ring_head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&ring_tail, memory_order_acquire);

    if (RING_SIZE - (h - t) < n) {         /* full: drop rather than block */
        atomic_fetch_add_explicit(&dropped_samples, n, memory_order_relaxed);
        return;
    }
    for (size_t i = 0; i < n; i++)
        ring[(h + i) & (RING_SIZE - 1)] = src[i];
    atomic_store_explicit(&ring_head, h + n, memory_order_release);
}

static int ring_pop(float *dst, size_t n)
{
    size_t h = atomic_load_explicit(&ring_head, memory_order_acquire);
    size_t t = atomic_load_explicit(&ring_tail, memory_order_relaxed);

    if (h - t < n) return 0;
    for (size_t i = 0; i < n; i++)
        dst[i] = ring[(t + i) & (RING_SIZE - 1)];
    atomic_store_explicit(&ring_tail, t + n, memory_order_release);
    return 1;
}

/* ---- DSP: replace the body of this with your processing ---- */
static void process(const float *in, float *out, unsigned long n)
{
    for (unsigned long i = 0; i < n; i++)
        out[i] = in[i];
}

/* ---- PortAudio callback (real-time thread: no malloc, no I/O, no locks) ---- */
static int audio_cb(const void *input, void *output, unsigned long frames,
                    const PaStreamCallbackTimeInfo *ti,
                    PaStreamCallbackFlags flags, void *user)
{
    const float *in  = (const float *)input;
    float       *out = (float *)output;
    size_t       n   = frames * CHANNELS;
    (void)ti; (void)flags; (void)user;

    if (in) process(in, out, n);
    else    memset(out, 0, n * sizeof(float));

    ring_push(out, n);                     /* tap the processed output */
    return paContinue;
}

/* ---- UDP sender thread ---- */
static void sender_loop(void)
{
    sock_t s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) { fprintf(stderr, "socket() failed\n"); return; }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(UDP_PORT);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    float pkt[PACKET_SAMPLES];
    while (atomic_load(&running)) {
        if (ring_pop(pkt, PACKET_SAMPLES))
            sendto(s, (const char *)pkt, sizeof pkt, 0,
                   (struct sockaddr *)&dst, sizeof dst);
        else
            SLEEP_MS(1);
    }
    CLOSE_SOCK(s);
}

#ifdef _WIN32
static DWORD WINAPI sender_thread(LPVOID arg) { (void)arg; sender_loop(); return 0; }
#else
static void *sender_thread(void *arg) { (void)arg; sender_loop(); return NULL; }
#endif

/* ---- Helpers ---- */
static void list_devices(void)
{
    int count = Pa_GetDeviceCount();
    for (int i = 0; i < count; i++) {
        const PaDeviceInfo *d = Pa_GetDeviceInfo(i);
        printf("[%d] %s  (%s)  in:%d out:%d\n", i, d->name,
               Pa_GetHostApiInfo(d->hostApi)->name,
               d->maxInputChannels, d->maxOutputChannels);
    }
}

static int check(PaError err, const char *what)
{
    if (err != paNoError) {
        fprintf(stderr, "%s: %s\n", what, Pa_GetErrorText(err));
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    PaStream *stream = NULL;
    int rc = 1;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    if (!check(Pa_Initialize(), "Pa_Initialize")) return 1;

    if (argc > 1 && strcmp(argv[1], "--list") == 0) {
        list_devices();
        Pa_Terminate();
        return 0;
    }

    PaStreamParameters inp, outp;
    memset(&inp, 0, sizeof inp);
    memset(&outp, 0, sizeof outp);
    inp.device  = (argc > 2) ? atoi(argv[1]) : Pa_GetDefaultInputDevice();
    outp.device = (argc > 2) ? atoi(argv[2]) : Pa_GetDefaultOutputDevice();
    if (inp.device == paNoDevice || outp.device == paNoDevice) {
        fprintf(stderr, "No default audio device. Try --list.\n");
        goto done;
    }
    inp.channelCount  = outp.channelCount = CHANNELS;
    inp.sampleFormat  = outp.sampleFormat = paFloat32;
    inp.suggestedLatency  = Pa_GetDeviceInfo(inp.device)->defaultLowInputLatency;
    outp.suggestedLatency = Pa_GetDeviceInfo(outp.device)->defaultLowOutputLatency;

    if (!check(Pa_OpenStream(&stream, &inp, &outp, SAMPLE_RATE, FRAMES_PER_BUF,
                             paClipOff, audio_cb, NULL), "Pa_OpenStream"))
        goto done;

#ifdef _WIN32
    HANDLE th = CreateThread(NULL, 0, sender_thread, NULL, 0, NULL);
#else
    pthread_t th;
    pthread_create(&th, NULL, sender_thread, NULL);
#endif

    if (!check(Pa_StartStream(stream), "Pa_StartStream")) {
        atomic_store(&running, 0);
    } else {
        printf("Running at %d Hz, mono. Streaming float32 to udp://127.0.0.1:%d\n",
               SAMPLE_RATE, UDP_PORT);
        printf("Press Enter to stop.\n");
        getchar();
        rc = 0;
    }

    atomic_store(&running, 0);
    Pa_StopStream(stream);
#ifdef _WIN32
    WaitForSingleObject(th, INFINITE);
    CloseHandle(th);
#else
    pthread_join(th, NULL);
#endif

    if (atomic_load(&dropped_samples))
        fprintf(stderr, "Warning: %lu samples dropped (UDP sender fell behind)\n",
                atomic_load(&dropped_samples));

done:
    if (stream) Pa_CloseStream(stream);
    Pa_Terminate();
#ifdef _WIN32
    WSACleanup();
#endif
    return rc;
}
