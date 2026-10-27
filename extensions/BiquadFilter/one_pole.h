// One-pole / first-order IIR filter module (companion to Biquad.h)
//
// Difference equation (direct form I):
//   y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1]
//
// Note: a first-order filter has a fixed slope (6 dB/oct), so there is no Q.

typedef struct _onepole{
    
    float a1;
    float b0;
    float b1;
    float prev_input;
    float prev_output;
    
    const char* type;
}onepole;

typedef enum {
    
    OP_LOWPASS  = 0,
    OP_HIGHPASS = 1,
    OP_ALLPASS  = 2,
    OP_LOWSHELF = 3,
    
    OP_HIGHSHELF = 4,
} ONEPOLE_TYPES;

// frequency: -3 dB point (LP/HP), 90 degree phase point (allpass),
//            or midpoint of the transition (shelves), in Hz.
// dbGain:    only used by the shelf types. Ignored otherwise.
// Returns NULL on allocation failure or if frequency is not in (0, sample_rate/2).
onepole* op_new(int filter_type, float frequency, float dbGain, int sample_rate);

float op_process(onepole* op, float input);

void op_destroy(onepole* op);

void op_print_info(onepole* op);

void op_load_coefficients(onepole* op, int filter_type, double A, double K);
