#include <math.h>
#include <stdio.h>
#include "Biquad.h"
#include "AC_normal.h"

#define pi_float	3.14159265f
#define AC_Normal

void normal_channel_init(AC_normal_channel* norm_chan, int given_sample_rate) {

	float cut_off_Hz = 5.0f;
	
	norm_chan->sample_rate = given_sample_rate;
	norm_chan->fixed_HPF = bq_new(HIGHPASS, 70, 0.66, 1, norm_chan->sample_rate);
	norm_chan->fixed_LPF = bq_new(LOWPASS, 9000, 0.66, 1, norm_chan->sample_rate);
	norm_chan->DC_alpha = 1.0f - (2.0f * pi_float * cut_off_Hz / norm_chan->sample_rate);

	norm_chan->previous_input = 0.0f;
	norm_chan->previous_output = 0.0f;
}

float normal_process(float signal, AC_normal_channel* norm_chan) {

    float fixed_gain = 4.5f;        //modelling the fixed gain you would get from a tube transistor without a potentiometer in the circuit
    float DC_bias = 0.25f;			//boosts the signal, resulting in assymetric clipping later
	float out;						//output for this cycle

	//Modeling the AC30's linear filtering, before the pre-amp
	out = bq_process(norm_chan->fixed_HPF, signal);
	out = bq_process(norm_chan->fixed_LPF, out);

    //apply gain
    out *= fixed_gain;

    //add DC bias
    out += DC_bias;

    //clip the signal with the function f(x) = x / 1 + |x|
    out = out / (1 + fabsf(out));

	float dc_blocker_input = out;

    //next we need a 1st order IIR filter to remove the DC bias added earlier
	//the equation of the filter being: y[n] = x[n] - x[n-1] + (alpha * y[n-1])
	out = dc_blocker_input - norm_chan -> previous_input + (norm_chan -> DC_alpha * norm_chan -> previous_output);

	//update filter variables
	norm_chan -> previous_input = dc_blocker_input;
	norm_chan -> previous_output = out;

	return out;
}