#ifndef SCOPE_TAP_H
#define SCOPE_TAP_H

// The oscilloscope tap. It copies audio out of the callback and streams it over UDP to the
// scope panel (oscilloscope.py), which listens on SCOPE_PORT.
//
// It has two halves, like the UI listener, but the data flows the other way:
//   - scope_write() is called from the audio callback. It copies samples into a lock-free
//     ring buffer and returns. It never blocks, allocates, or touches the network.
//   - A sender thread empties the ring buffers every few milliseconds and sends the samples
//     to the scope as UDP datagrams.
//
// The scope has two channels, so two points in the signal path can be compared (for example
// the dry input against the output of an effect).
//
// The panel shows the same samples two ways: against time (the oscilloscope) and against
// frequency (the spectrum analyser). The frequency analysis is all done in the panel, so it
// costs the audio thread nothing more than the oscilloscope does.
//
// The panel can also switch on a test signal (a sine wave, a sweep or noise) in place of the
// input. The generator itself lives in test_signals.c. This module only carries the panel's
// commands to it, and switches it off if the panel goes quiet or the sender stops.

#define SCOPE_PORT              9000            // The UDP port the scope panel listens on
#define SCOPE_NUM_CHANNELS      2               // How many traces the scope can show

#define SCOPE_CH_A              0               // By convention: the reference signal (e.g. the dry input)
#define SCOPE_CH_B              1               // By convention: the signal under test (e.g. the processed output)

// HOW TO USE
//
//     // 1. Start the sender (once, before starting the audio stream).
//     start_scope_sender(SAMPLE_RATE);
//
//     // 2. In the audio path, hand the scope the MONO buffers you want to look at:
//     scope_write(SCOPE_CH_A, in_buf, buffer_size);       // before the effects
//     scope_write(SCOPE_CH_B, out_buf, buffer_size);      // after the effects
//
//     // 3. When shutting down:
//     stop_scope_sender();
//
// To look at one effect on its own, move the two scope_write() calls so they sit either
// side of that effect's process function.
//
// To be able to use the test signal, add one more call (from test_signals.h), before the first scope_write():
//     test_signal_process(out_buf, buffer_size);          // overwrites the buffer, but only while the generator is on
//     scope_write(SCOPE_CH_A, out_buf, buffer_size);      // channel A now shows whichever signal went in

// Creates the socket and starts the sender thread. sample_rate is passed on to the scope,
// which needs it to turn samples into time, and to the test signal generator (test_signal_init).
// Returns 0 on success (or if it is already running) and -1 on failure.
// Failing is not fatal to the audio side: scope_write() simply does nothing.
// Call this before starting the audio stream.
int start_scope_sender(int sample_rate);

// Stops the thread and closes the socket.
// Safe to call even if start_scope_sender() was never called or failed, and safe to call
// while the audio stream is still running.
void stop_scope_sender(void);

// Copies count samples to one of the scope's channels. Lock-free, so it is safe to call
// from the audio callback. The buffer must be mono (one float per frame).
//
// Call it once per channel, per callback, with the same count for every channel that is in
// use. The scope lines the channels up by counting samples, so this is what keeps them in
// step with each other.
//
// Does nothing if the sender isn't running, or if channel is invalid. If the sender thread
// falls behind and the ring buffer is full, the whole block is dropped (and counted) rather
// than making the audio thread wait.
void scope_write(int channel, const float *samples, int count);

// THE COMMANDS
//
// The panel controls the test signal by sending a line of text back to the socket the samples
// come from. The sender thread hands each one to test_signal_command(); the commands themselves
// are listed in test_signals.h. The panel repeats its message once a second, and that is what
// keeps the generator on: if nothing valid arrives for SCOPE_GEN_TIMEOUT_MS, the sender switches it off.

// THE WIRE FORMAT
//
// Each datagram carries a block of samples from ONE channel: a 24 byte header, then the
// samples. Everything is little-endian (the byte order of x86, so it is copied as-is).
//
//     offset  size  field
//     0       4     "SCP1"         marks this as a scope packet (and its version)
//     4       2     channel        0 = A, 1 = B
//     6       2     count          how many samples follow
//     8       4     sample_rate    in Hz
//     12      4     dropped        samples this channel has dropped so far (ring buffer full)
//     16      8     index          the position of the first sample, counted in samples
//                                  since the sender started. Both channels count on the
//                                  same clock, which is how the scope lines them up
//     24      4*n   samples        count 32-bit floats

#endif
