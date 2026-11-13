#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include "ui_listener.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// Windows reports "port unreachable" on a UDP socket as an error on the NEXT recvfrom, which
// would happen every time we send to a UI that has closed. This ioctl turns that off.
// MinGW doesn't always define it, so it is spelled out here.
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#define CONTROL_PORT            9001            // The UDP port number the UI will send control signals to
#define CONTROLS_FILE    "src/ui/controls.json" // Written when the listener starts and deleted when it stops. The UI reads it to build itself.
#define MAX_NAME_LENGTH         15              // The longest control name that parse_message can read (it reads 15 characters)
#define LISTEN_TIMEOUT_US       20000           // How long the listener waits for a message before checking for display values to send (20 ms)
#define DISPLAY_RESEND_MS       1000            // Every display value is re-sent this often, in case a UDP packet was lost
#define MAX_ROTARY_POSITIONS    100             // The most positions a rotary control can have. Values travel with 3 decimals, which keeps this many apart

//The combined list of every control registered by every module
//Slot i of each array belongs to control i. The index a module gets back from register_controls() is its first slot
static const ControlDef *g_defs[MAX_CONTROLS];  // each control's definition. These point into the modules' own tables
static int g_rows[MAX_CONTROLS];                // the UI row each control appears in, after stacking the modules
static int g_count = 0;                         // how many controls are registered

//The control sets ("groups"). Each call to register_controls() makes one group. The UI draws an
//outline around a group's controls, with the group's label on the top left
//A group always has at least one control, so there can never be more groups than controls
static int g_group_of[MAX_CONTROLS];            // which group each control belongs to
static const char *g_group_label[MAX_CONTROLS]; // each group's label (the engine's name)
static const char *g_group_colour[MAX_CONTROLS]; // each group's colour (the engine's colour). NULL = the UI's default
static int g_group_count = 0;                   // how many groups are registered

//Variables shared between the UI listener thread, and the callback thread
//These variables are written to by the listener thread, and read by the callback thread
static _Atomic float g_values[MAX_CONTROLS];    // the latest value of each control

//Going the other way: written by the callback thread (set_display_value), read by the listener thread
static _Atomic float g_display[MAX_CONTROLS];   // the value each setter last applied, in real units. NAN = not known yet

static _Atomic int g_running = 0;           // Listener loop runs while g_running = 1
static SOCKET g_sock = INVALID_SOCKET;      // holds the socket pointer
static HANDLE g_thread = NULL;              // holds the thread pointer

//Only the listener thread touches these, so they need no atomics
static struct sockaddr_in g_ui_addr;        // where the UI's last valid message came from, so we know where to reply
static int g_have_ui = 0;                   // 1 once a valid message has arrived
static float g_sent[MAX_CONTROLS];          // the display value last sent for each control

//Registering a module's controls
//All the checks happen before anything is added, so a bad table never leaves a module half registered
//expects an opaque pointer to the related effects engine
//int register_controls(const ControlDef *defs2, int count2, effects_engine* engine) {
int register_controls(effects_engine* engine) {

	const ControlDef* defs = engine->controls;
	int count = engine->num_controls;

	if (g_thread != NULL) {
		fprintf(stderr, "register_controls: must be called before start_control_listener()\n");
		return -1;
	}
	if (defs == NULL || count <= 0) {
		fprintf(stderr, "register_controls: the control table is missing or empty\n");
		return -1;
	}
	if (g_count + count > MAX_CONTROLS) {
		fprintf(stderr, "register_controls: too many controls (the maximum is %d)\n", MAX_CONTROLS);
		return -1;
	}

	for (int i = 0; i < count; i++) {
		const char *name = defs[i].name;

		//The name has to be something parse_message can read back: 1 to 15 characters, no spaces
		if (name == NULL || name[0] == '\0' || strlen(name) > MAX_NAME_LENGTH || strpbrk(name, " \t\r\n") != NULL) {
			fprintf(stderr, "register_controls: control %d has an invalid name (it must be 1 to %d characters, with no spaces)\n", i, MAX_NAME_LENGTH);
			return -1;
		}
		if (defs[i].row < 0) {
			fprintf(stderr, "register_controls: control \"%s\" has a negative row\n", name);
			return -1;
		}
		if (!(defs[i].default_value >= 0.0f && defs[i].default_value <= 1.0f)) {    //written this way so NaN fails too
			fprintf(stderr, "register_controls: control \"%s\" has a default outside 0.0 to 1.0\n", name);
			return -1;
		}
		if (defs[i].type == CTRL_ROTARY && (defs[i].positions < 2 || defs[i].positions > MAX_ROTARY_POSITIONS)) {
			fprintf(stderr, "register_controls: rotary control \"%s\" needs 2 to %d positions\n", name, MAX_ROTARY_POSITIONS);
			return -1;
		}

		//Names are the key on the wire, so each one must be unique across ALL modules
		for (int j = 0; j < g_count; j++) {
			if (strcmp(name, g_defs[j]->name) == 0) {
				fprintf(stderr, "register_controls: the name \"%s\" is already registered\n", name);
				return -1;
			}
		}
		for (int j = 0; j < i; j++) {
			if (strcmp(name, defs[j].name) == 0) {
				fprintf(stderr, "register_controls: the name \"%s\" appears twice in one table\n", name);
				return -1;
			}
		}
	}

	//Everything checked out, so add the controls
	int base = g_count;
	int last_row = 0;

	//This engine's controls form one group on the UI, labelled and coloured by the engine
	int group = g_group_count;
	g_group_label[group] = engine->name;
	g_group_colour[group] = engine->colour;
	g_group_count++;

	for (int i = 0; i < count; i++) {

		g_defs[base + i] = &defs[i];																		//Adds a Control Def object to the list held above
		g_group_of[base + i] = group;																		//Records which group the control belongs to
		g_rows[base + i] = defs[i].row;        																//stack add this module to it's designated row
		atomic_store_explicit(&g_values[base + i], defs[i].default_value, memory_order_relaxed);			//Sets the value in the shared params array
		atomic_store_explicit(&g_display[base + i], NAN, memory_order_relaxed);							//Nothing to show until the setter has run
		update_funcs[base + i] = defs[i].update_func;
		e_points[base + i] = engine;
		
		if (defs[i].row > last_row) {
			
			last_row = defs[i].row;
		}
	}
	g_count += count;

	return 0;		// return base;
}

//Atomic access to the shared variables
//These do nothing other than wrap access to these variables
float get_control(int index) {

	if (index < 0 || index >= g_count) {
		return 0.0f;                        // not a valid control
	}
	return atomic_load_explicit(&g_values[index], memory_order_relaxed);
}

const char *get_control_name(int index) {

	if (index < 0 || index >= g_count) {
		return "?";
	}
	return g_defs[index]->name;
}

param_update get_control_func(int index) {

	return update_funcs[index];
}

int get_control_count(void) {

	return g_count;
}

void set_display_value(int index, float value) {

	if (index < 0 || index >= g_count) {
		return;                             // not a valid control
	}
	atomic_store_explicit(&g_display[index], value, memory_order_relaxed);
}

//cleans up the failed socket setup
static void socket_setup_failure() {

	if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; }
	WSACleanup();
}

//parse a message from the UI
//Returns 1 if it named a registered control, 0 if it was ignored
static int parse_message(const char *message) {

	char name[16];
	float value;

	if (sscanf(message, "%15s %f", name, &value) != 2){
		return 0;                           //message string is malformed, ignore it
	}
	if (!isfinite(value)) {
		return 0;                           //check for NaN or infinite values
	}
	if (value < 0.0f) {
		value = 0.0f;                       //This should be impossible
	}
	if (value > 1.0f) {
		value = 1.0f;                       //but better safe than sorry...
	}

	//Look the name up in the combined list. If a control matches, store the value in its slot
	//This loops over every registered control, so it knows nothing about any particular effect
	for (int i = 0; i < g_count; i++) {
		if (strcmp(name, g_defs[i]->name) == 0) {
			atomic_store_explicit(&g_values[i], value, memory_order_relaxed);
			return 1;
		}
	}
	//No control matched, so the name is unknown and the message is ignored
	return 0;
}

/*  Sending display values back to the UI
*   Each one goes as its own datagram, in the same "name value" form the UI sends us:
*       "trm_rate 1.57"
*   Only controls with units are sent, and only once the setter has reported a value.
*   With force = 0, only values that changed since they were last sent go out.
*   With force = 1, everything goes out (the periodic resend, or a new UI appeared).
*   Called from the listener thread only.
*/
static void send_display_values(int force) {

	if (!g_have_ui) {
		return;                             //nobody to send to yet
	}

	char msg[64];
	for (int i = 0; i < g_count; i++) {
		if (g_defs[i]->units == NULL) {
			continue;                       //this control has no value line on the UI
		}
		float v = atomic_load_explicit(&g_display[i], memory_order_relaxed);
		if (!isfinite(v)) {
			continue;                       //not known yet
		}
		if (!force && v == g_sent[i]) {
			continue;                       //unchanged
		}

		int len = snprintf(msg, sizeof msg, "%s %.6g", g_defs[i]->name, v);
		if (len > 0 && len < (int)sizeof msg) {
			sendto(g_sock, msg, len, 0, (const struct sockaddr *)&g_ui_addr, sizeof g_ui_addr);
		}
		g_sent[i] = v;                      //a failed send is caught by the next periodic resend
	}
}

/*  Describing the controls: writing controls.json
*   The file looks like this, with one group and one control per line. JSON is a plain
*   text format that Python can read directly. A control's "group" is a position in the
*   "groups" list, counting from 0:
*   {"groups": [
*     {"label": "distortion", "colour": "#d98c2b"},
*     ...
*   ],
*   "controls": [
*     {"name": "gain", "label": "Gain", "type": "knob", "group": 0, "row": 0, "default": 0.500, "units": "dB"},
*     {"name": "stages", "label": "Stages", "type": "rotary", "group": 0, "row": 0, "default": 0.000, "positions": 12, "units": ""},
*     ...
*   ]}
*/
static const char *type_name(ControlType t) {

	switch (t) {
		case CTRL_SWITCH:   return "switch";
		case CTRL_ROTARY:   return "rotary";
		default:            return "knob";
	}
}

//Writes a C string as a JSON string: in quotes, with " and \ escaped, and any invisible
//control characters written as \uXXXX. Without this, a quote in a label would break the file
static void write_json_string(FILE *f, const char *s) {

	fputc('"', f);
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		if (c == '"' || c == '\\') {
			fprintf(f, "\\%c", c);
		}
		else if (c < 0x20) {
			fprintf(f, "\\u%04x", c);
		}
		else {
			fputc(c, f);
		}
	}
	fputc('"', f);
}

//The same, but a NULL string is written as JSON's null, which tells the UI "not set"
static void write_json_string_or_null(FILE *f, const char *s) {

	if (s != NULL) {
		write_json_string(f, s);
	}
	else {
		fputs("null", f);
	}
}

static int write_controls_file(void) {

	FILE *f = fopen(CONTROLS_FILE, "w");
	if (f == NULL) {
		return -1;
	}

	//The groups come first, so the UI knows each one's label and colour
	fputs("{\"groups\": [\n", f);
	for (int i = 0; i < g_group_count; i++) {

		fputs(i > 0 ? ",\n  " : "  ", f);   //commas go BETWEEN entries
		fputs("{\"label\": ", f);
		write_json_string_or_null(f, g_group_label[i]);
		fputs(", \"colour\": ", f);
		write_json_string_or_null(f, g_group_colour[i]);     //null tells the UI to use its default colour
		fputc('}', f);
	}

	fputs("\n],\n\"controls\": [\n", f);
	for (int i = 0; i < g_count; i++) {
		const ControlDef *d = g_defs[i];

		fputs(i > 0 ? ",\n  " : "  ", f);   //commas go BETWEEN entries
		fputs("{\"name\": ", f);
		write_json_string(f, d->name);
		fputs(", \"label\": ", f);
		write_json_string(f, d->label ? d->label : d->name);
		fprintf(f, ", \"type\": \"%s\", \"group\": %d, \"row\": %d, \"default\": %.3f",
				type_name(d->type), g_group_of[i], g_rows[i], d->default_value);
		if (d->type == CTRL_ROTARY) {
			fprintf(f, ", \"positions\": %d", d->positions);     //only a rotary has fixed positions
		}
		fputs(", \"units\": ", f);
		write_json_string_or_null(f, d->units);             //null tells the UI not to show a value line
		fputc('}', f);
	}
	fputs("\n]}\n", f);

	return fclose(f) == 0 ? 0 : -1;
}

/*  The listener thread
* Windows requires a thread function to look exactly like this
* nothing is passed into the thread, so args is unused
*/
static DWORD WINAPI listener_thread(LPVOID arg) {

	char buf[256];
	(void)arg;

	DWORD last_resend = GetTickCount();

	while (atomic_load_explicit(&g_running, memory_order_relaxed)) {

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(g_sock, &fds);
		struct timeval tv = {0, LISTEN_TIMEOUT_US};

		if (select(0, &fds, NULL, NULL, &tv) > 0) {

			struct sockaddr_in from;
			int from_len = sizeof from;
			int n = recvfrom(g_sock, buf, sizeof buf - 1, 0, (struct sockaddr *)&from, &from_len);

			if (n > 0) {
				buf[n] = '\0';
				if (parse_message(buf)) {
					//A valid message tells us where the UI is. If it's a new UI (or the panel
					//was restarted on a new port), send it everything straight away
					int is_new = !g_have_ui
								 || from.sin_port != g_ui_addr.sin_port
								 || from.sin_addr.s_addr != g_ui_addr.sin_addr.s_addr;
					g_ui_addr = from;
					g_have_ui = 1;
					if (is_new) {
						send_display_values(1);
						last_resend = GetTickCount();
					}
				}
			}
		}

		//Whether or not a message arrived, pass on any values the setters have changed
		//Unsigned subtraction keeps this correct when GetTickCount wraps (every 49 days)
		DWORD now = GetTickCount();
		if (now - last_resend >= DISPLAY_RESEND_MS) {
			send_display_values(1);
			last_resend = now;
		}
		else {
			send_display_values(0);
		}
	}
	return 0;
}

//Starts the listener thread

int start_control_listener(void) {

	if (g_thread != NULL) {             // checks if the thread is running
		return 0;
	}
	if (g_count == 0) {
		fprintf(stderr, "start_control_listener: no controls are registered. Call register_controls() first\n");
		return -1;
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

	//Stop a closed UI from turning into recvfrom errors (see SIO_UDP_CONNRESET above)
	//If this fails nothing breaks, the listener just ignores the odd failed recvfrom
	BOOL report_reset = FALSE;
	DWORD bytes_returned = 0;
	WSAIoctl(g_sock, SIO_UDP_CONNRESET, &report_reset, sizeof report_reset, NULL, 0, &bytes_returned, NULL, NULL);

	//Forget any UI from a previous run
	g_have_ui = 0;

	//If we've reached this point, thread setup has been successful
	//Now we start the listener thread
	atomic_store_explicit(&g_running, 1, memory_order_relaxed);
	g_thread = CreateThread(NULL, 0, listener_thread, NULL, 0, NULL);
	if (g_thread == NULL) {
		atomic_store_explicit(&g_running, 0, memory_order_relaxed);
		socket_setup_failure();
		return -1;
	}

	//Everything is up and listening, so now tell the UI what controls exist
	//Doing this last means the UI can never build itself and start sending before we are ready to receive
	//Failing here is not fatal. The audio side still works, there is just no UI layout
	if (write_controls_file() != 0) {
		fprintf(stderr, "Warning: could not write %s. The UI will not be able to build itself\n", CONTROLS_FILE);
	}

	return 0;
}

//stop and clean up the thread
void stop_control_listener(void) {

	if (g_thread == NULL) {                     // if the thread ain't running, don't stop it
		return;
	}

	atomic_store(&g_running, 0);                // set the loop flag to allow the thread to end
	WaitForSingleObject(g_thread, INFINITE);    // wait for the listener_thread to exit
	CloseHandle(g_thread);                      // release the thread
	g_thread = NULL;
	closesocket(g_sock);                        // close the socket
	g_sock = INVALID_SOCKET;
	WSACleanup();                               // call cleanup explicitly
	remove(CONTROLS_FILE);                      // the effect is gone, so delete its description
}
