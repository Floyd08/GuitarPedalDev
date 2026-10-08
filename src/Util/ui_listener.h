#ifndef UI_LISTENER_H
#define UI_LISTENER_H

float get_gain();
float get_volume();
float get_bias();
float get_bass_gain();
float get_mid_gain();
float get_treble_gain();
float get_bass_freq();
float get_mid_freq();
float get_treble_freq();

int start_control_listener();
void stop_control_listener();

#endif