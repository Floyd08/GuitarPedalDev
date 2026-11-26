#ifndef CONTROL_DEF_H
#define CONTROL_DEF_H

// The shared description of a control. Effect modules (distortion, EQ, ...) include this
// to declare their controls. It has no networking code, so the DSP code stays independent
// of the UI listener.

// Effects Module row numbers, centralized here
#define		COMPRESSOR_ROW_NUM			0
#define		MASTER_FX_ROW_NUM			0
#define		EQ_ROW_NUM1					0
#define		EQ_ROW_NUM2					1
#define		DISTORTION_ROW_NUM			1
#define		AMP_ROW_NUM					2
#define		MODULATION_ROW_NUM			3
#define		AMPLITUDE_ROW_NUM			3
#define		DELAY_ROW_NUM				4
#define		VERB_ROW_NUM				4

// macros for ctrl_switch
#define		OFF							0
#define		ON							1

typedef struct effects_engine effects_engine;   			// forward declaration; the full struct is in effects_engine.h
// The setter type. Takes the UI's 0.0 to 1.0 value, applies it to the engine, and returns
// the value it actually applied, in the parameter's own units (Hz, dB, ms, ...).
// The returned value is what the UI shows under the percentage.
typedef float (*param_update)(effects_engine*, float);

// The kinds of control the UI knows how to draw
typedef enum {
	CTRL_KNOB,                  // a dial, any value from 0.0 to 1.0
	CTRL_SWITCH,                // an on/off button, 0.0 = off and 1.0 = on
	CTRL_ROTARY                 // a dial that clicks between a fixed number of positions (set by "positions" below).
								// The positions are spread evenly from 0.0 to 1.0, so with N positions,
								// position i (counting from 0) has the value i / (N - 1)
} ControlType;

// Describes one control. A module defines a table of these at file scope (not as a local
// variable, because the listener keeps pointers into it) and registers it with
// register_controls() in ui_listener.h.
typedef struct {
	const char *name;           // the text the UI sends over UDP, e.g. "gain".
								// 1 to 15 characters, no spaces, unique across ALL modules
	const char *label;          // the text the UI shows above the control, e.g. "Gain"
	ControlType type;           // CTRL_KNOB, CTRL_SWITCH or CTRL_ROTARY
	int row;                    // which row of this module's controls it goes in. 0 is the first row
	float default_value;        // the starting value, 0.0 to 1.0 (use 0.0 or 1.0 for a switch)
	param_update update_func;	// A pointer to the setter associated with this parameter in the effects engine
	const char *units;          // the units of the setter's return value, shown by the UI, e.g. "Hz" or "dB".
								// NULL (the default if left out) shows no value line.
								// "" shows the number with no units (e.g. a plain mix factor)
	int positions;              // CTRL_ROTARY only: how many positions the dial has, 2 to 100. Ignored by other types
} ControlDef;

#endif
