/*
 * sine_udp_commented.c
 *
 * A heavily commented version of sine_udp.c. Behaviour is identical.
 *
 * WHAT IT DOES
 *   Generates a sine wave in software and sends it, as a stream of raw
 *   32-bit floating point samples, over UDP to port 9000 on the local machine
 *   (127.0.0.1). Another program (such as scope.py) can listen on that port
 *   and display or analyze the signal.
 *
 * THE STREAM FORMAT
 *   - Each UDP packet holds PACKET_SIZE samples (256 by default).
 *   - Each sample is a 4-byte IEEE-754 float, so each packet is 1024 bytes.
 *   - There is no header, no sequence number, and no framing. The receiver
 *     must already know the sample rate (48000 Hz) and that the data is mono.
 *   - The bytes are sent exactly as they sit in memory. On x86 and ARM
 *     machines that is little-endian, which is what scope.py expects.
 *
 * BUILD
 *   Linux/macOS: gcc -O2 -std=c11 sine_udp_commented.c -o sine_udp -lm
 *   MinGW:       gcc -O2 -std=c11 sine_udp_commented.c -o sine_udp.exe -lws2_32
 *   MSVC:        cl /O2 sine_udp_commented.c ws2_32.lib
 *
 *   -lm links the math library (needed for sin() on Linux/macOS).
 *   ws2_32 is the Windows sockets library.
 *
 * STOP
 *   Press Ctrl+C. The program loops forever, and the operating system
 *   closes the socket when the process exits.
 */


/* ------------------------------------------------------------------------
 * Feature-test macro (non-Windows only)
 * ------------------------------------------------------------------------
 * When you compile with -std=c11, the C library hides functions that are not
 * part of the C standard itself. nanosleep() (used below to sleep) is a POSIX
 * function, so we ask for the POSIX 2008 definitions. This line MUST come
 * before any #include, because the headers read this macro as they are
 * processed. On Windows the macro is not needed, so we skip it.
 */
#ifndef _WIN32
  #define _POSIX_C_SOURCE 200809L
#endif


/* ------------------------------------------------------------------------
 * Standard headers (available on every platform)
 * ------------------------------------------------------------------------
 */
#include <math.h>     /* sin()                                              */
#include <stdio.h>    /* printf(), fprintf()                                */
#include <string.h>   /* memset()                                           */
#include <time.h>     /* struct timespec, clock_gettime() (POSIX)           */


/* ------------------------------------------------------------------------
 * Platform-specific headers and the 1 ms sleep macro
 * ------------------------------------------------------------------------
 * Windows and POSIX systems (Linux, macOS) use different socket headers and
 * different sleep functions. We hide those differences behind the macro
 * SLEEP_1MS(), so the rest of the program is the same on every platform.
 */
#ifdef _WIN32
  /* Windows: Winsock. winsock2.h must be included BEFORE windows.h. */
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>

  /* Sleep() takes milliseconds. Note that its real resolution on Windows can
   * be coarser than 1 ms (often around 15 ms by default). The main loop is
   * written to cope with that, as explained further down. */
  #define SLEEP_1MS() Sleep(1)

#else
  /* POSIX: Berkeley sockets. */
  #include <arpa/inet.h>    /* htons(), htonl(), INADDR_LOOPBACK            */
  #include <netinet/in.h>   /* struct sockaddr_in                           */
  #include <sys/socket.h>   /* socket(), sendto()                           */

  /* nanosleep() takes a struct timespec: { seconds, nanoseconds }.
   * The expression (struct timespec){0, 1000000L} is a "compound literal",
   * an unnamed temporary struct holding 0 seconds and 1,000,000 ns = 1 ms.
   * The second argument (NULL) means we don't care how much time was left
   * if the sleep is interrupted by a signal. */
  #define SLEEP_1MS() nanosleep(&(struct timespec){0, 1000000L}, NULL)
#endif


/* ------------------------------------------------------------------------
 * Configuration. Change these to alter the test signal.
 * ------------------------------------------------------------------------
 */
#define PORT         9000     /* UDP destination port on localhost          */
#define SAMPLE_RATE  48000    /* samples per second                         */
#define FREQ_HZ      1000.0   /* frequency of the sine wave in Hz           */
#define AMPLITUDE    0.5      /* peak value; 1.0 is full scale (0 dBFS), so
                                 0.5 is about -6 dBFS                       */
#define PACKET_SIZE  256      /* samples per UDP packet. At 48 kHz that is
                                 256 / 48000 = 5.33 ms of audio per packet  */


/* ------------------------------------------------------------------------
 * now_seconds()
 * ------------------------------------------------------------------------
 * Returns a time in seconds as a double. The zero point is arbitrary (for
 * example, system boot), so the value is only meaningful when you subtract
 * two readings, which is all we do.
 *
 * We use a MONOTONIC clock: one that only ever moves forward and is not
 * affected by the user or the network changing the system time. That is the
 * right choice for measuring elapsed time. (A wall clock can jump, which
 * would make the pacing loop below send a burst or stall.)
 *
 * We don't use C11's timespec_get() because some toolchains (notably older
 * MinGW) don't provide it even with -std=c11. Each platform has a native
 * high-resolution clock instead:
 *
 *   Windows: QueryPerformanceCounter() returns a tick count, and
 *            QueryPerformanceFrequency() returns ticks per second, so
 *            count / frequency gives seconds.
 *   POSIX:   clock_gettime(CLOCK_MONOTONIC) fills in a struct timespec with
 *            whole seconds (tv_sec) and nanoseconds (tv_nsec), which we
 *            combine into one floating point number.
 *
 * The values can be large, but a double has about 15-16 significant digits,
 * which is plenty of precision for differences measured in microseconds.
 */
#ifdef _WIN32
static double now_seconds(void)
{
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}
#else
static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif


int main(void)
{
    /* --------------------------------------------------------------------
     * Windows only: start up Winsock
     * --------------------------------------------------------------------
     * Windows requires you to initialize the sockets library before using
     * it. MAKEWORD(2, 2) requests version 2.2. POSIX systems need nothing.
     */
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    /* --------------------------------------------------------------------
     * Create the UDP socket
     * --------------------------------------------------------------------
     *   AF_INET     = IPv4
     *   SOCK_DGRAM  = datagram socket, i.e. UDP (as opposed to SOCK_STREAM,
     *                 which would be TCP)
     *   0           = pick the default protocol for this type (UDP)
     *
     * UDP is "connectionless": there is no handshake, and we never need to
     * know whether anyone is listening. If no program is receiving on port
     * 9000, the packets are silently discarded.
     *
     * On Windows socket() returns a SOCKET (an unsigned integer type); we
     * store it in an int for simplicity. A failed call returns -1 once
     * converted, so the < 0 test works on both platforms.
     */
    int s = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        fprintf(stderr, "socket() failed\n");
        return 1;
    }

    /* --------------------------------------------------------------------
     * Build the destination address: 127.0.0.1, port 9000
     * --------------------------------------------------------------------
     * memset() zeroes the whole struct first, so that any fields we don't
     * set explicitly (including padding) are 0.
     *
     * Network protocols use "network byte order" (big-endian), but most
     * computers are little-endian. htons() ("host to network short") and
     * htonl() ("host to network long") convert the port and address to the
     * correct byte order.
     *
     * INADDR_LOOPBACK is 127.0.0.1, the "loopback" address. Traffic sent
     * there never leaves your computer, which is exactly what we want for
     * passing a signal between two programs on the same machine.
     */
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family      = AF_INET;
    dst.sin_port        = htons(PORT);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    /* --------------------------------------------------------------------
     * Sine generator state
     * --------------------------------------------------------------------
     * A sine wave completes one full cycle when its argument advances by
     * 2*pi radians. We keep a running "phase" (the current argument) and add
     * a fixed "step" for every sample:
     *
     *      step = 2*pi * frequency / sample_rate
     *
     * For 1000 Hz at 48000 Hz that is 2*pi/48, so one cycle takes exactly
     * 48 samples.
     *
     * The phase is kept between packets (it is declared outside the loop),
     * so the waveform continues smoothly from the end of one packet into the
     * start of the next, with no jumps.
     *
     * We use double for phase and step because float accumulates rounding
     * error over millions of additions, which would slowly detune the tone.
     */
    const double two_pi = 6.283185307179586;
    const double step   = two_pi * FREQ_HZ / SAMPLE_RATE;
    double phase = 0.0;

    /* Buffer holding one packet's worth of samples. sizeof packet is
     * PACKET_SIZE * 4 = 1024 bytes, which is what we pass to sendto(). */
    float packet[PACKET_SIZE];

    printf("Sending %.0f Hz sine (%d Hz, float32) to 127.0.0.1:%d. Ctrl+C to stop.\n",
           FREQ_HZ, SAMPLE_RATE, PORT);

    /* --------------------------------------------------------------------
     * Real-time pacing
     * --------------------------------------------------------------------
     * We want to send audio at the correct speed: 48000 samples every real
     * second. If we sent as fast as possible, the receiver would be flooded.
     *
     * The simplest method (send a packet, then sleep for 5.33 ms) is
     * inaccurate: sleeps usually run a little long, and the error adds up
     * on every packet, so the stream drifts slower than real time. Windows
     * is especially bad because Sleep() can overshoot by many milliseconds.
     *
     * Instead we use the clock as the source of truth:
     *   - 'start' is the moment we began.
     *   - 'sent'  counts how many samples we have sent so far.
     *   - Each time round the loop we work out how many samples SHOULD have
     *     been sent by now (elapsed time * sample rate), and send packets
     *     until we have caught up.
     *
     * If a sleep runs long, we simply send a few packets back-to-back to
     * catch up, so the long-term average rate stays correct.
     */
    double start = now_seconds();
    unsigned long long sent = 0;   /* 64-bit so it never overflows in practice */

    for (;;) {
        /* Number of samples we owe the receiver by this moment in time. */
        double due = (now_seconds() - start) * SAMPLE_RATE;

        /* Send whole packets for as long as a full packet is due.
         * 'sent + PACKET_SIZE <= due' means "if I sent one more packet, I
         * would still not be ahead of the clock". */
        while ((double)(sent + PACKET_SIZE) <= due) {

            /* Fill the packet with the next PACKET_SIZE samples. */
            for (int i = 0; i < PACKET_SIZE; i++) {
                /* sin() returns -1..+1; multiply by AMPLITUDE to scale it,
                 * then convert from double to the float we transmit. */
                packet[i] = (float)(AMPLITUDE * sin(phase));

                /* Advance the phase for the next sample, and wrap it back
                 * into 0..2*pi. Wrapping keeps the number small, so sin()
                 * stays accurate even after hours of running. Subtracting
                 * 2*pi does not change the sine's value. */
                phase += step;
                if (phase >= two_pi) phase -= two_pi;
            }

            /* Transmit the packet as one UDP datagram.
             *   - The cast to const char* is required by Winsock's
             *     prototype; POSIX accepts it too.
             *   - The return value is ignored on purpose: for a test signal
             *     we don't care if a send fails (for example, nothing
             *     listening yet).
             * Each call sends exactly one datagram, and the receiver gets it
             * whole (UDP preserves message boundaries), 1024 bytes here. */
            sendto(s, (const char *)packet, sizeof packet, 0,
                   (struct sockaddr *)&dst, sizeof dst);

            sent += PACKET_SIZE;
        }

        /* Nothing more is due yet: sleep about 1 ms so we don't spin the CPU
         * at 100%. A packet is due every 5.33 ms, so checking every 1 ms is
         * frequent enough to keep the stream smooth. */
        SLEEP_1MS();
    }

    /* Never reached (the loop above is infinite), which is why there is no
     * closesocket()/WSACleanup() here. The OS reclaims everything when the
     * process is stopped. */
}
