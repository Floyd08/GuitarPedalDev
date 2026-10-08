#ifndef EQ_FX_H
#define EQ_FX_H

#include "Biquad.h"

#define		DFLT_LS_CF			80		//The default cutoff frequency of the Low Shelf filter
#define		DFLT_HS_CF			10000	//The default cutoff frequency of the high Shelf filter

#define		DFLT_MP_MIN			500		//The minimum frequency of the mid sweep
#define		DFLT_MP_CENTRE		1500	//The default centre frequency of the mid peaking filter
#define		DFLT_MP_MAX			2500	//The maximum frequency of the mid sweep
#define		MID_Q				1		//Q set for the mid peaking filter

#define		BUTTERWORTH_Q		0.7071	//Used for the high and low shelf filter

#define 	MIN_GAIN_EQ			-15		//Used in the input conversion
#define 	MAX_GAIN_EQ			+15
#define		DFLT_GAIN			0.5		// Unity gain

typedef struct {

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
void process_EQ(EQ_engine* eq, float *in_buf, float *out_buf, int buffer_size);

void EQ_update_filter_gain(EQ_engine* eq, float new_gain, FILTER_TYPES f_type);
void EQ_update_filter_centre(EQ_engine* eq, float new_centre_freq, FILTER_TYPES f_type);

float EQ_convert_Hz(float value, float new_min, float new_max);
float EQ_convert_dB(float value);

void EQ_free(EQ_engine* eq);

#endif