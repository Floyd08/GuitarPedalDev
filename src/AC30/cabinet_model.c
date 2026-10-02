#include <string.h>
#include "cabinet_model.h"

void cabinet_model_init(cabinet_model* cab_mod) {

	memset(cab_mod->irCoefficients, 0, sizeof(cab_mod->irCoefficients));
    memset(cab_mod->historyBuffer, 0, sizeof(cab_mod->historyBuffer));

	 // Set up a basic default flat impulse pass-through state
    cab_mod->irCoefficients[0] = 1.0f; 
    cab_mod->irLength = 1;
    cab_mod->writeIndex = 0;
}

void cabinet_model_loadIR(cabinet_model* cab_mod, const float* customIR, int length)
{
    cab_mod->irLength = (length > MAX_IR_LENGTH) ? MAX_IR_LENGTH : length;
    memcpy(cab_mod->irCoefficients, customIR, cab_mod->irLength * sizeof(float));
    memset(cab_mod->historyBuffer, 0, sizeof(cab_mod->historyBuffer)); // Reset history buffer
    cab_mod->writeIndex = 0;
}

float cabinet_model_process(cabinet_model* cab_mod, float inputSample)
{
    // 1. Store incoming sample in current circular ring buffer head position
    cab_mod->historyBuffer[cab_mod->writeIndex] = inputSample;
    
    float accumulatedOutput = 0.0f;
    int readIndex = cab_mod->writeIndex;
    
    // 2. Perform the direct multiply-accumulate dot product (FIR Filter loop)
    for (int i = 0; i < cab_mod->irLength; ++i)
    {
        accumulatedOutput += cab_mod->historyBuffer[readIndex] * cab_mod->irCoefficients[i];
        
        // Loop backward through circular ring buffer memory history
        readIndex--;
        if (readIndex < 0) {
            readIndex = MAX_IR_LENGTH - 1;
        }
    }
    
    // 3. Advance circular ring write head position
    cab_mod->writeIndex++;
    if (cab_mod->writeIndex >= MAX_IR_LENGTH) {
        cab_mod->writeIndex = 0;
    }
    
    return accumulatedOutput;
}