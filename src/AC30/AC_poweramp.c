#include "AC_poweramp.h"
#include <math.h>

void poweramp_init (AC_poweramp* poweramp, float sample_rate) {

	poweramp->sample_rate = sample_rate;
	poweramp->envelop_state = 0.0f;

	//calculate envelope coefficients from sample rate: 10ms attack, 100ms release
	poweramp->attack = 1.0f - expf(-1.0f / (0.010 * poweramp->sample_rate));
	poweramp->release = 1.0f - expf(-1.0f / (0.100 * poweramp->sample_rate));
}

float poweramp_process (AC_poweramp* poweramp, float signal, float volume, float power_sag) {

	float absolute_signal, target_sag, output, crossover_deadzone, pos_signal, neg_signal; 

	signal *= volume;

	//Compute the envelope follower
	absolute_signal = fabsf(signal);
	if (absolute_signal > poweramp->envelop_state) {
		poweramp->envelop_state += poweramp->attack * (absolute_signal - poweramp->envelop_state);
	}
	else {
		poweramp->envelop_state += poweramp->release * (absolute_signal - poweramp->envelop_state);
	}

	//Use the value of the envelope follower to govern the sag effect
	//As the value increases, the sag effect inscreases(as the virtual voltage drops)
	target_sag = 1.0f - (poweramp->envelop_state * 0.4f * power_sag);
	if (target_sag < 0.5f) {
		target_sag = 0.5f;			//prevents the signal from going to 0
	} 

	//Model phase inversion, and crossover distortion
	output = 0.0f; crossover_deadzone = 0.01f;

	if (signal > crossover_deadzone) {
		//apply sag to postive half-wave
		pos_signal = (signal - crossover_deadzone) / target_sag;
		output = pos_signal / (1.0f + fabsf(pos_signal)) * target_sag;
	}
	else if (signal < -crossover_deadzone) {
		//apply sag to negative half-wave
		neg_signal = (signal + crossover_deadzone) / target_sag;
		output = neg_signal / (1.0f + fabsf(neg_signal)) * target_sag;
	}
	else {
		//Crossover region
		output = 0.0f;
	}

	return output;
}

