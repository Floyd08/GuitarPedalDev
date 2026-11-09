#ifndef UI_LISTENER_H
#define UI_LISTENER_H

#include "control_def.h"
#include "effects_engine.h"

#define MAX_CONTROLS 64             // the most controls that can be registered, across all modules

extern param_update update_funcs[MAX_CONTROLS];
extern effects_engine* e_points[MAX_CONTROLS];

// HOW TO USE
//
//     // 1. Each module registers its control table (once, before starting the listener).
//     //    The return value is the index of that module's first control. Keep it.
//     int dist_base = register_controls(distortion_controls, DIST_NUM_CONTROLS);
//     int eq_base   = register_controls(eq_controls, EQ_NUM_CONTROLS);
//
//     // 2. Start the listener, then open and start the audio stream.
//     start_control_listener();
//
//     // 3. In the audio callback, read values by (module base + the module's own enum):
//     float gain = get_control(dist_base + DIST_CTRL_GAIN);
//
//     // 4. When shutting down, stop the audio stream first, then:
//     stop_control_listener();
//
// With a single module the base is 0, so get_control(DIST_CTRL_GAIN) works directly.

// Adds a module's controls to the listener's combined list.
// Returns the index of the module's first control, or -1 if nothing was registered
// (the reason is printed to stderr). It fails if:
//   - the listener has already started
//   - the table is NULL, empty, or would exceed MAX_CONTROLS
//   - a name is empty, longer than 15 characters, or contains a space
//   - a name is already used, by this table or an earlier one (names are the key on the wire)
//   - a row is negative, or a default value is outside 0.0 to 1.0
// Each module's rows are stacked below the previous module's, so two modules never share
// a row on the UI. Every control holds its default value as soon as it is registered.
// The table must stay valid for as long as the listener is used.
// Call this from one thread, before starting the audio stream. It is not thread-safe.
int register_controls(effects_engine* engine);
//int register_controls(const ControlDef *defs, int count, effects_engine* engine);

// Creates the socket, binds it, starts the listener thread, and writes controls.json
// (a description of every registered control, which the UI reads to build itself).
// Returns 0 on success (or if it is already running) and -1 on failure, for example if
// nothing is registered or the port is already in use. Failing to write controls.json
// only prints a warning.
int start_control_listener(void);

// Stops the thread, closes the socket, and deletes controls.json.
// Safe to call even if start_control_listener() was never called or failed.
void stop_control_listener(void);

// The latest value of a control, 0.0 to 1.0. Lock-free, so it is safe to call from the
// audio callback. A switch is 0.0 (off) or 1.0 (on). An invalid index returns 0.0.
float get_control(int index);

// The text name of a control (e.g. "gain"), handy for printing. An invalid index returns "?".
const char *get_control_name(int index);

param_update get_control_func(int index);

// How many controls are registered so far.
int get_control_count(void);

#endif
