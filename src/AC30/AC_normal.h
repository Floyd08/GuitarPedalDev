#ifndef AC_NORMAL_CHANNEL_H
#define AC_NORMAL_CHANNEL_H

#include "Biquad.h"

typedef struct AC_normal_channel
{
	int sample_rate;
	float previous_output;
	float previous_input;
	float DC_alpha;
	biquad* fixed_HPF;
	biquad* fixed_LPF;

} AC_normal_channel;

void normal_channel_init(AC_normal_channel* norm_chan, int given_sample_rate);
float normal_process(float signal, AC_normal_channel* norm_chan);

#endif