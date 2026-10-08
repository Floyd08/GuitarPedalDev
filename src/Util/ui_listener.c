#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include "ui_listener.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define CONTROL_PORT			9001			// The UDP port number the UI will send control signals to
#define DEFAULT_GAIN			0.5				// gain used until a control signal says otherwise
#define DEFAULT_VOLUME			0.5				// volume used until a control signal says otherwise
#define DEFAULT_BIAS			0.5				// bias used until a control signal says otherwise
#define DEFAULT_BASS_GAIN		0.5				// band gain for bass knob of the EQ
#define DEFAULT_MID_GAIN		0.5				// band gain for mid knob of the EQ
#define DEFAULT_TREBLE_GAIN		0.5				// band gain for bass knob of the EQ
#define DEFAULT_BASS_FREQ		0.5				// default centre frequency for the low shelf filter
#define DEFAULT_MID_FREQ		0.5				// default centre frequency for the mid peak filter
#define DEFAULT_TREBLE_FREQ		0.5				// default centre frequency for the high shelf filter

//Variables shared between the UI listener thread, and the callback thread
//These variables are written to by the listener thread, and read by the callback thread

static _Atomic float g_gain = DEFAULT_GAIN;
static _Atomic float g_volume = DEFAULT_VOLUME;
static _Atomic float g_bias = DEFAULT_BIAS;
static _Atomic float g_bass_gain = DEFAULT_BASS_GAIN;
static _Atomic float g_mid_gain = DEFAULT_MID_GAIN;
static _Atomic float g_treble_gain = DEFAULT_TREBLE_GAIN;
static _Atomic float g_bass_freq = DEFAULT_BASS_FREQ;
static _Atomic float g_mid_freq = DEFAULT_MID_FREQ;
static _Atomic float g_treble_freq = DEFAULT_TREBLE_FREQ;

static _Atomic int g_running = 0;			// Listener loop runs while g_running = 1
static SOCKET g_sock = INVALID_SOCKET;		// holds the socket pointer
static HANDLE g_thread = NULL;				// holds tge thread pointer

//Atomic access to the shared variables
//These do nothing other than wrap access to these variables
float get_gain() {
	return atomic_load_explicit(&g_gain, memory_order_relaxed);
}

float get_volume() {
	return atomic_load_explicit(&g_volume, memory_order_relaxed);
}

float get_bias() {
	return atomic_load_explicit(&g_bias, memory_order_relaxed);
}

float get_bass_gain() {
	return atomic_load_explicit(&g_bass_gain, memory_order_relaxed);
}

float get_mid_gain() {
	return atomic_load_explicit(&g_mid_gain, memory_order_relaxed);
}

float get_treble_gain() {
	return atomic_load_explicit(&g_treble_gain, memory_order_relaxed);
}

float get_bass_freq() {
	return atomic_load_explicit(&g_bass_freq, memory_order_relaxed);
}

float get_mid_freq() {
	return atomic_load_explicit(&g_mid_freq, memory_order_relaxed);
}

float get_treble_freq() {
	return atomic_load_explicit(&g_treble_freq, memory_order_relaxed);
}

//cleans up the failed socket setup
static void socket_setup_failure() {
	
	if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; }
    WSACleanup();
}

//parse a message from the UI
static void parse_message(const char *message) {

	// printf("parsing control message\n");
	// printf("message: %s\n", message);
	char name[16];
	float value;

	if (sscanf(message, "%15s %f", name, &value) != 2){
		return;								//message string is malformed, ignore it
	}
	if (!isfinite(value)) {
		return;								//check for NaN or infinite values
	}
	if (value < 0.0f) {
		value = 0.0f;						//This should be impossible
	}
    if (value > 1.0f) {
		value = 1.0f;						//but better safe than sorry...
	}

	//This should eventually be turned into a switch statement
	//An enumerated structure may be useful later, as it will be easier to change out the UI layer that way
	//A simple hash function may be ideal instead
	if (strcmp(name, "gain") == 0) {
		atomic_store_explicit(&g_gain, value, memory_order_relaxed);
	}
	else if (strcmp(name, "volume") == 0) {
		atomic_store_explicit(&g_volume, value, memory_order_relaxed);
	}
	else if (strcmp(name, "bias") == 0) {
		atomic_store_explicit(&g_bias, value, memory_order_relaxed);
	}
	else if (strcmp(name, "bass") == 0) {
		atomic_store_explicit(&g_bass_gain, value, memory_order_relaxed);
	}
	else if (strcmp(name, "mid") == 0) {
		atomic_store_explicit(&g_mid_gain, value, memory_order_relaxed);
	}
	else if (strcmp(name, "treble") == 0) {
		atomic_store_explicit(&g_treble_gain, value, memory_order_relaxed);
	}
	else if (strcmp(name, "bass_sweep") == 0) {
		atomic_store_explicit(&g_bass_freq, value, memory_order_relaxed);
	}
	else if (strcmp(name, "mid_sweep") == 0) {
		atomic_store_explicit(&g_mid_freq, value, memory_order_relaxed);
	}
	else if (strcmp(name, "treble_sweep") == 0) {
		atomic_store_explicit(&g_treble_freq, value, memory_order_relaxed);
	}
	// As controllable parameters are added, this structure will grow
}

/*	The listener thread
* Windows requires a thread function to look exactly like this
* nothing is passed into the thread, so args is unused
*/
static DWORD WINAPI listener_thread(LPVOID arg) {

	char buf[256];
	(void)arg;

	while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
		
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(g_sock, &fds);
		struct timeval tv = {0, 100000};
		if (select(0, &fds, NULL, NULL, &tv) <= 0) {
			continue;
		}

		int n = recvfrom(g_sock, buf, sizeof buf - 1, 0, NULL, NULL);
		if (n <= 0) {
			continue;
		}
		buf[n] = '\0';
		parse_message(buf);
	}
	return 0;
}

//Starts the listener thread

int start_control_listener() {

	if (g_thread != NULL) {				// checks if the thread is running
		return 0;
	}

	// Windows requires this call before any socket function.
    // each call is matched by a WSACleanup.
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		return -1;
	}
	// Create a UDP socket (SOCK_DGRAM = UDP, AF_INET = IPv4)
	g_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_sock == INVALID_SOCKET) {
		//cleans up the failed socket setup
		socket_setup_failure();
		return -1;
	}

	// Listen on the port defined above locally (127.0.0.1) 
	struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONTROL_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(g_sock, (struct sockaddr *)&addr, sizeof addr) != 0) {
		socket_setup_failure();
		return -1;
	}

	//If we've reached this point, thread setup has been successful
	//Now we start the listener thread
	atomic_store_explicit(&g_running, 1, memory_order_relaxed);
	g_thread = CreateThread(NULL, 0, listener_thread, NULL, 0, NULL);
	if (g_thread == NULL) {
		atomic_store_explicit(&g_running, 0, memory_order_relaxed);
		socket_setup_failure();
		return -1;
	}
	
	return 0;
}

//stop and clean up the thread
void stop_control_listener() {

	if (g_thread == NULL) {						// if the thread ain't running, don't stop it
		return;
	}

	atomic_store(&g_running, 0);				// set the loop flag to allow the thread to end
	WaitForSingleObject(g_thread, INFINITE);	// wait for the listener_thread to exit
	CloseHandle(g_thread);						// release the thread
	g_thread = NULL;
	closesocket(g_sock);						// close the socket
	g_sock = INVALID_SOCKET;
	WSACleanup();								// call cleanup explicitly
}