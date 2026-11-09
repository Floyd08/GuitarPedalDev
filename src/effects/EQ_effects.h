#ifndef EQ_FX_H
#define EQ_FX_H

#include "Biquad.h"
#include "control_def.h"

#define		DFLT_LS_CF			0.5		//The default cutoff frequency of the Low Shelf filter
#define		DFLT_LS_MIN			20		//The minimum frequency of the bass sweep
#define		DFLT_LS_MAX			400		//The maximum frequency of the bass sweep

#define		DFLT_HS_CF			0.5		//The default cutoff frequency of the high Shelf filter
#define		DFLT_HS_MIN			2000	//The minimum frequency of the mid sweep
#define		DFLT_HS_MAX			20000	//The maximum frequency of the mid sweep

#define		DFLT_MP_CF			0.5		//The default centre frequency of the mid peaking filter
#define		DFLT_MP_MIN			500		//The minimum frequency of the treble sweep
#define		DFLT_MP_MAX			2500	//The maximum frequency of the treble sweep

#define		MID_Q				1		//Q set for the mid peaking filter
#define		BUTTERWORTH_Q		0.7071	//Used for the high and low shelf filter

#define 	MIN_GAIN_EQ			-15		//Used in the input conversion
#define 	MAX_GAIN_EQ			+15
#define		DFLT_GAIN			0.5		// Unity gain

enum {
	EQ_CTRL_B_GAIN,						// Bass gain control
	EQ_CTRL_M_GAIN,						// Mid gain control
	EQ_CTRL_T_GAIN,						// Treble gain control
	EQ_CTRL_B_FREQ,						// Bass freq sweep control
	EQ_CTRL_M_FREQ,						// Bass freq sweep control
	EQ_CTRL_T_FREQ,						// Bass freq sweep control
	EQ_NUM_CONTROLS
};
extern const ControlDef EQ_controls[EQ_NUM_CONTROLS];

typedef struct {

	effects_engine base;				//included to imitate a supertype
	biquad* lsf;						//Low shelf filter
	float lsf_gain;						//current gain value for LSF
	float lsf_centre;					//current setting of the bass sweep control
	biquad* hsf;						//High shelf filter
	float hsf_gain;						//current gain value for HSF
	float hsf_centre;					//current setting of the treble sweep control
	biquad* mpf;						//mid peaking filter
	float mpf_gain;						//current gain value for mpf
	float mpf_centre;					//current setting of the mid sweep control
	int sample_rate;					//sample rate of the callback function

} EQ_engine;

void EQ_init(EQ_engine* eq, int sample_rate);
void process_EQ(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size);

// void EQ_update_filter_gain(EQ_engine* eq, float new_gain, FILTER_TYPES f_type);
// void EQ_update_filter_centre(EQ_engine* eq, float new_centre_freq, FILTER_TYPES f_type);

void EQ_update_lsf_gain(effects_engine* engine, float new_gain);
void EQ_update_hsf_gain(effects_engine* engine, float new_gain);
void EQ_update_mpf_gain(effects_engine* engine, float new_gain);

void EQ_update_lsf_centre(effects_engine* engine, float new_gain);
void EQ_update_mpf_centre(effects_engine* engine, float new_gain);
void EQ_update_hsf_centre(effects_engine* engine, float new_gain);

float EQ_convert_Hz(float value, float new_min, float new_max);
float EQ_convert_dB(float value);

void EQ_free(EQ_engine* eq);

#endif