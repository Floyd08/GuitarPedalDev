#include <portaudio.h>

int main () {
	return 0;
}

static int pa_passthrough_callback( const void *inputBuffer, void *outputBuffer, 
									unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, 
									PaStreamCallbackFlags statusFlags, void *userData) 
{
	float *mono_input = (float*)userData;
	float *out = (float*)outputBuffer;
	unsigned int i;
	(void) inputBuffer;

	i = 0;
	while  (i < framesPerBuffer) {
		if (mono_input != NULL) {
			out[i] = mono_input[i];
		}
		else {
			out[i] = 0.0f;
		}
		 
	}

	return paContinue;
}