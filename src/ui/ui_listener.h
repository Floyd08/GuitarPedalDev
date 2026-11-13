#ifndef UI_LISTENER_H
#define UI_LISTENER_H

#include "control_def.h"
#include "effects_engine.h"

#define MAX_CONTROLS 64             // the most controls that can be registered, across all modules

extern param_update update_funcs[MAX_CONTROLS];
extern effects_engine* e_points[MAX_CONTROLS];

// HOW TO USE
//
//     // 1. Each module registers its engine's control table (once, before starting the listener).
//     register_controls(&d_engine.base);
//
//     // 2. Start the listener, then open and start the audio stream.
//     start_control_listener();
//
//     // 3. In the audio callback, when control i has changed, call its setter and report
//     //    the value it applied, so the UI can show it in real units:
//     float shown = get_control_func(i)(e_points[i], get_control(i));
//     set_display_value(i, shown);
//
//     // 4. When shutting down, stop the audio stream first, then:
//     stop_control_listener();

// Adds a module's controls to the listener's combined list.
// Returns 0, or -1 if nothing was registered (the reason is printed to stderr). It fails if:
//   - the listener has already started
//   - the table is NULL, empty, or would exceed MAX_CONTROLS
//   - a name is empty, longer than 15 characters, or contains a space
//   - a name is already used, by this table or an earlier one (names are the key on the wire)
//   - a row is negative, or a default value is outside 0.0 to 1.0
//   - a CTRL_ROTARY control has fewer than 2 or more than 100 positions
//
// Each call makes one control set on the UI: an outline around the engine's controls, with
// engine->name as a label on the top left. engine->colour sets the colour of both the outline
// and the label. It is "#rrggbb" or a colour name such as "orange"; NULL gives the UI's
// default grey.
//
// Rows are used exactly as the table gives them. A set is placed on the panel at its lowest
// row, sets that start on the same row sit side by side, and a set whose controls use
// several rows shows them as rows inside its outline.
// Every control holds its default value as soon as it is registered.
// The table, the name and the colour must stay valid for as long as the listener is used.
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
// audio callback. A switch is 0.0 (off) or 1.0 (on). A rotary with N positions gives
// position i (counting from 0) as i / (N - 1), rounded to 3 decimals, so convert it back
// with roundf(value * (N - 1)). An invalid index returns 0.0.
float get_control(int index);

// The text name of a control (e.g. "gain"), handy for printing. An invalid index returns "?".
const char *get_control_name(int index);

param_update get_control_func(int index);

// Reports the value a control's setter actually applied, in the control's units (the
// setter's return value). The listener thread sends it to the UI, which shows it under
// the percentage. Lock-free, so it is safe to call from the audio callback, and it does
// no networking itself. Only controls whose ControlDef has units are sent.
// An invalid index is ignored.
void set_display_value(int index, float value);

// How many controls are registered so far.
int get_control_count(void);

#endif
