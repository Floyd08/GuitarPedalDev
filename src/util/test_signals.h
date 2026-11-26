#ifndef TEST_SIGNALS_H
#define TEST_SIGNALS_H

// The test signal generator. It plays a test signal (a sine wave, a sweep or noise) in place of
// the input, which is what frequency measurements really want: a sine shows up an effect's
// harmonics and aliasing, a sweep shows aliasing at every frequency in one pass, and noise shows
// the frequency response.
//
// It has two sides:
//   - test_signal_process() is called from the audio callback. It writes the test signal into
//     the buffer that feeds the effects. It never blocks, allocates, or touches the network.
//   - The other functions are called from outside the audio thread. The scope sender
//     (scope_tap.c) passes on the panel's commands, and switches the generator off when the
//     panel goes quiet or the sender stops.
//
// The module has no operating-system code of its own, so it doesn't depend on the scope: any
// module that can deliver the commands below can drive it.

// HOW TO USE
//
//     // 1. Set the sample rate (once, before starting the audio stream).
//     //    start_scope_sender() does this for you.
//     test_signal_init(SAMPLE_RATE);
//
//     // 2. In the audio path, on the MONO buffer that feeds the effects:
//     test_signal_process(out_buf, buffer_size);          // overwrites the buffer, but only while the generator is on
//     scope_write(SCOPE_CH_A, out_buf, buffer_size);      // channel A now shows whichever signal went in
//
//     // 3. Pass on any command text from the panel:
//     test_signal_command(message);

// Sets the sample rate, switches the generator off, and resets it: the frequency, level and
// sweep length go back to their defaults, and the signal starts again from the beginning
// (phase, noise and sweep) the next time it is switched on.
// A signal that is playing stops at once, without a fade.
// Safe to call at any time, even while the audio stream is running: the audio callback
// clears its own state at the start of its next block.
void test_signal_init(int sample_rate);

// The audio callback's side. Call it on the MONO buffer that feeds the effects. While the
// generator is off (which it is until a command turns it on) this does nothing and returns 0,
// so the guitar passes as usual. While it is on, it REPLACES the contents of the buffer with
// the test signal and returns 1.
//
// It fades in and out over a few milliseconds, so switching it never clicks.
// It also does nothing until test_signal_init() has set a sample rate.
// Lock-free, so it is safe to call from the audio callback.
int test_signal_process(float *samples, int count);

// Reads a command (see THE COMMANDS below) and applies it.
// Returns 1 if it was a valid command, 0 if it was ignored.
// Call it from one thread only (the scope sender does this).
int test_signal_command(const char *message);

// Switches the generator off. It fades out rather than stopping dead.
// Safe to call from any thread except the audio callback, at any time.
void test_signal_off(void);

// Returns 1 if a test signal has been asked for, 0 if the generator is off.
// (It can still be fading out for a few milliseconds after this returns 0.)
int test_signal_is_on(void);

// THE COMMANDS
//
// The generator is controlled by a line of text, in the same style as the UI listener's messages:
//
//     "gen sine 1000 -20"      type, frequency in Hz, level in dB (0 dB = a peak of 1.0)
//     "gen sweep 0 -20 10"     the same, with an optional 4th field: how long a sweep takes, in seconds
//
// The type is off, sine, white, pink or sweep. The frequency only matters for a sine, but is
// always sent. A sweep is a sine that glides from 20 Hz up to 20 kHz (logarithmically, so every
// octave takes the same time), then starts again; the panel shows it best in its spectrogram.
//
// The scope panel sends these to the scope sender's socket and repeats its message once a second.
// The scope sender switches the generator off if the panel goes quiet for SCOPE_GEN_TIMEOUT_MS,
// so closing the panel can never leave a tone playing.

#endif
