#ifndef CABINET_MODEL_H
#define CABINET_MODEL_H

#define MAX_IR_LENGTH 512

typedef struct {
    float irCoefficients[MAX_IR_LENGTH]; // Array holding loaded cabinet IR data
    float historyBuffer[MAX_IR_LENGTH];  // Ring buffer memory for input history
    int irLength;                        // Actual length of loaded IR (up to 512)
    int writeIndex;                      // Current write head position
} cabinet_model;

void cabinet_model_init(cabinet_model* cab_mod);

/**
 * @brief Direct injection of an IR array curve (e.g. from an embedded header file)
 */
void cabinet_model_loadIR(cabinet_model* cab_mod, const float* customIR, int length);

/**
 * @brief Standard direct time-domain FIR convolution filter
 */
float cabinet_model_process(cabinet_model* cab_mod, float inputSample);

#endif