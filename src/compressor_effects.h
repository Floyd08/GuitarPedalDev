#include "effects_engine.h"
#include "control_def.h"

#define	COMP_ATCK_MIN				0.1		//defined range for the attack control in milliseconds
#define	COMP_ATCK_MAX				100
#define DFLT_ATCK					0.3
#define COMP_RLSE_MIN				5		//defined range for the release control in milliseconds
#define COMP_RLSE_MAX				2000
#define DFLT_RLSE					0.3
#define	DFLT_THRSH					0.5
#define	DFLT_MKGAIN					0.75
#define DFLT_RATIO					0.45

// enumerates the controls that the UI should generate for this fx module
enum {
	COMP_CTRL_ATCK,
	COMP_CTRL_RLSE,
	COMP_CTRL_THRSH,
	COMP_CTRL_MKGAIN,
	COMP_CTRL_RATIO,
	COMP_NUM_CONTROLS
};
extern const ControlDef compressor_controls[COMP_NUM_CONTROLS];

typedef struct {

	effects_engine base;						// included to imitate a supertype
	envelope_follower* e_fol;					// An envelope follower object
	float attack;								// The attack in milliseconds
	float release;								// The release in milliseconds
	float threshold;							// The linear threshold over which compression is triggered. Must be > 0
	float makeup_gain;							// The maximum input gain to be applied, which gets scaled by the state of the envelope during compression
	float ratio;								// Used to calculate the esponent, needed in gain calculation must be >= 1
	float exponent;								// Pre-computated to avoid division in real-time
	int sample_rate;
} compressor_engine;

void compressor_init(compressor_engine* c_engine, envelope_follower* e_fol, int sample_rate);
void process_compressor(effects_engine* engine, float *in_buf, float *out_buf, int buffer_size);

void set_comp_attack(effects_engine* engine, float attack);
void set_comp_release(effects_engine* engine, float release);
void set_makeup_gain(effects_engine* engine, float gain);
void set_threshold(effects_engine* engine, float threshold);
void set_ratio(effects_engine* engine, float ratio);