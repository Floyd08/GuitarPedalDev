#ifndef AC_POWERAMP_H
#define AC_POWERAMP_H

typedef struct {				// Consider replacing this with a generalized envelop follower. I will need more of 'em
	float sample_rate;
	float envelop_state;		//Current value of the envelop
	float attack;				//Attack smoothing
	float release;				//Release smoothing
} AC_poweramp;

void poweramp_init (AC_poweramp* poweramp, float sample_rate);

/**
* @brief power sag wants to be around 0.5 - 0.8. Always between 0 - 1
*		and volume should be between 1.5 - 4.0
*/
float poweramp_process (AC_poweramp* poweramp, float signal, float volume, float power_sag);

#endif