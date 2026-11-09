#ifndef CONTROL_DEF_H
#define CONTROL_DEF_H

// The shared description of a control. Effect modules (distortion, EQ, ...) include this
// to declare their controls. It has no networking code, so the DSP code stays independent
// of the UI listener.

typedef struct effects_engine effects_engine;   			// forward declaration; the full struct is in effects_engine.h
typedef void (*param_update)(effects_engine*, float);		// define a function type, to accept update function pointers from an arbitrary effects module

// The kinds of control the UI knows how to draw
typedef enum {
	CTRL_KNOB,                  // a dial, any value from 0.0 to 1.0
	CTRL_SWITCH                 // an on/off button, 0.0 = off and 1.0 = on
} ControlType;

// Describes one control. A module defines a table of these at file scope (not as a local
// variable, because the listener keeps pointers into it) and registers it with
// register_controls() in ui_listener.h.
typedef struct {
	const char *name;           // the text the UI sends over UDP, e.g. "gain".
								// 1 to 15 characters, no spaces, unique across ALL modules
	const char *label;          // the text the UI shows above the control, e.g. "Gain"
	ControlType type;           // CTRL_KNOB or CTRL_SWITCH
	int row;                    // which row of this module's controls it goes in. 0 is the first row
	float default_value;        // the starting value, 0.0 to 1.0 (use 0.0 or 1.0 for a switch)
	param_update update_func;	// A pointer to the setter associated with this parameter in the effects engine
} ControlDef;

#endif
