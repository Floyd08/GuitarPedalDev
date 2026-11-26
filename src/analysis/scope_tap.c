#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include "scope_tap.h"
#include "test_signals.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

// Windows reports "port unreachable" on a UDP socket as an error on the NEXT recvfrom, which
// would happen every time we send samples while the panel is closed. This ioctl turns that off.
// MinGW doesn't always define it, so it is spelled out here (as in ui_listener.c).
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#define SCOPE_RING_SIZE         16384           // Samples held per channel while they wait to be sent. Must be a power of 2.
                                                // At 48kHz this is 340 ms, far longer than the sender thread ever sleeps
#define SCOPE_RING_MASK         (SCOPE_RING_SIZE - 1)
#define SCOPE_PACKET_SAMPLES    256             // The most samples sent in one datagram. 24 + 256 * 4 = 1048 bytes,
                                                // which stays under the usual 1500 byte limit for a single network packet
#define SCOPE_HEADER_BYTES      24              // The size of the packet header (see the wire format in scope_tap.h)
#define SCOPE_SEND_INTERVAL_MS  2               // How long the sender thread waits between sweeps of the ring buffers.
                                                // Windows may round this up to its timer tick (as much as 15.6 ms). That
                                                // only delays the picture slightly; no samples are lost
#define SCOPE_GEN_TIMEOUT_MS    3000            // The test signal switches off if the panel has sent no command for this long.
                                                // The panel repeats its command every second, so this only happens if it has gone

// One ring buffer per channel. Each has exactly one writer (the audio callback) and one
// reader (the sender thread), which is what lets it work without a lock:
//   - only the callback changes write_count
//   - only the sender thread changes read_count
// Both counts run freely (they are never wrapped back to 0), so write_count - read_count
// is always the number of samples waiting, even after the counts overflow.
// A count is masked with SCOPE_RING_MASK only when it is used as a position in data[]
typedef struct {

	float data[SCOPE_RING_SIZE];
	_Atomic unsigned int write_count;           // total samples written. Written by the callback thread
	_Atomic unsigned int read_count;            // total samples read. Written by the sender thread
	_Atomic unsigned int dropped;               // total samples thrown away because the ring was full. Written by the callback thread

} scope_ring;

static scope_ring g_scope_rings[SCOPE_NUM_CHANNELS];

static _Atomic int g_scope_running = 0;         // The sender loop runs, and scope_write accepts samples, while this is 1
static SOCKET g_scope_sock = INVALID_SOCKET;    // holds the socket
static HANDLE g_scope_thread = NULL;            // holds the thread
static struct sockaddr_in g_scope_addr;         // where the scope panel listens (127.0.0.1, SCOPE_PORT)
static int g_scope_sample_rate = 0;             // passed on to the scope in every packet

// The audio callback's side
// Copies a block into the channel's ring buffer. No locks, no system calls
void scope_write(int channel, const float *samples, int count) {

	if (!atomic_load_explicit(&g_scope_running, memory_order_relaxed)) {
		return;                                 // the sender isn't running, so nobody would read this
	}
	if (channel < 0 || channel >= SCOPE_NUM_CHANNELS || samples == NULL || count <= 0) {
		return;
	}

	scope_ring *ring = &g_scope_rings[channel];
	unsigned int n = (unsigned int)count;
	unsigned int write = atomic_load_explicit(&ring->write_count, memory_order_relaxed);
	unsigned int read = atomic_load_explicit(&ring->read_count, memory_order_acquire);

	//If the block doesn't fit, drop all of it. The audio thread must never wait for the sender
	unsigned int space = SCOPE_RING_SIZE - (write - read);
	if (n > space) {
		atomic_fetch_add_explicit(&ring->dropped, n, memory_order_relaxed);
		return;
	}

	//Copy in two pieces: up to the end of the array, then the rest from the start
	unsigned int position = write & SCOPE_RING_MASK;
	unsigned int first = SCOPE_RING_SIZE - position;
	if (first > n) {
		first = n;
	}
	memcpy(&ring->data[position], samples, first * sizeof(float));
	memcpy(&ring->data[0], samples + first, (n - first) * sizeof(float));

	//Publishing the new count is what makes the samples visible to the sender thread,
	//so it happens last, after the samples are in place
	atomic_store_explicit(&ring->write_count, write + n, memory_order_release);
}

// The sender thread's side
// Moves up to max_count waiting samples out of a ring buffer. Returns how many it moved
static unsigned int scope_ring_read(scope_ring *ring, float *out, unsigned int max_count) {

	unsigned int read = atomic_load_explicit(&ring->read_count, memory_order_relaxed);
	unsigned int write = atomic_load_explicit(&ring->write_count, memory_order_acquire);

	unsigned int n = write - read;              // how many samples are waiting
	if (n > max_count) {
		n = max_count;
	}
	if (n == 0) {
		return 0;
	}

	unsigned int position = read & SCOPE_RING_MASK;
	unsigned int first = SCOPE_RING_SIZE - position;
	if (first > n) {
		first = n;
	}
	memcpy(out, &ring->data[position], first * sizeof(float));
	memcpy(out + first, &ring->data[0], (n - first) * sizeof(float));

	//Publishing the new count hands the space back to the callback
	atomic_store_explicit(&ring->read_count, read + n, memory_order_release);
	return n;
}

// Fills in the packet header. The fields are copied in one at a time, rather than by
// casting the buffer to a struct, so the layout can't be changed by compiler padding
static void scope_write_header(unsigned char *packet, int channel, unsigned int count, unsigned int dropped, uint64_t index) {

	uint16_t channel16 = (uint16_t)channel;
	uint16_t count16 = (uint16_t)count;
	uint32_t rate32 = (uint32_t)g_scope_sample_rate;
	uint32_t dropped32 = (uint32_t)dropped;

	memcpy(packet + 0, "SCP1", 4);
	memcpy(packet + 4, &channel16, 2);
	memcpy(packet + 6, &count16, 2);
	memcpy(packet + 8, &rate32, 4);
	memcpy(packet + 12, &dropped32, 4);
	memcpy(packet + 16, &index, 8);
}

/*  The sender thread
* Windows requires a thread function to look exactly like this
* nothing is passed into the thread, so arg is unused
*/
static DWORD WINAPI scope_sender_thread(LPVOID arg) {

	(void)arg;

	//The packet is built in a float array so the samples land on a properly aligned address
	//The header takes the first 6 floats' worth of space (6 * 4 = 24 bytes), the samples follow
	float packet[SCOPE_HEADER_BYTES / sizeof(float) + SCOPE_PACKET_SAMPLES];
	float *payload = packet + SCOPE_HEADER_BYTES / sizeof(float);

	//Each channel's position on the sample clock: every sample it has sent, plus every sample
	//it has dropped. Counting the dropped ones keeps the channels lined up after an overflow
	uint64_t sent[SCOPE_NUM_CHANNELS] = {0};
	uint64_t dropped_total[SCOPE_NUM_CHANNELS] = {0};
	unsigned int dropped_seen[SCOPE_NUM_CHANNELS] = {0};

	char command[64];                           // a command from the panel
	DWORD last_command = GetTickCount();        // when the last valid one arrived

	while (atomic_load_explicit(&g_scope_running, memory_order_relaxed)) {

		//Empty the ring buffers, one packet from each channel in turn, so the channels arrive
		//at the scope side by side instead of one long burst after another
		int sent_any;
		do {
			sent_any = 0;
			for (int ch = 0; ch < SCOPE_NUM_CHANNELS; ch++) {

				scope_ring *ring = &g_scope_rings[ch];

				//Fold any newly dropped samples into this channel's position first.
				//Unsigned subtraction keeps this correct if the dropped counter wraps
				unsigned int dropped = atomic_load_explicit(&ring->dropped, memory_order_relaxed);
				dropped_total[ch] += (unsigned int)(dropped - dropped_seen[ch]);
				dropped_seen[ch] = dropped;

				unsigned int n = scope_ring_read(ring, payload, SCOPE_PACKET_SAMPLES);
				if (n == 0) {
					continue;
				}

				scope_write_header((unsigned char *)packet, ch, n, dropped, sent[ch] + dropped_total[ch]);
				sendto(g_scope_sock, (const char *)packet, (int)(SCOPE_HEADER_BYTES + n * sizeof(float)), 0,
					   (const struct sockaddr *)&g_scope_addr, sizeof g_scope_addr);

				//A failed send (or a scope that isn't running) just loses the block. That is the
				//right behaviour for a scope: old samples are no use, the next ones are on their way
				sent[ch] += n;
				sent_any = 1;
			}
		} while (sent_any);

		//Wait a moment before the next sweep. Waiting on the socket, rather than just sleeping,
		//means a command from the panel is picked up as soon as it arrives
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(g_scope_sock, &fds);
		struct timeval tv = {0, SCOPE_SEND_INTERVAL_MS * 1000};
		int ready = select((int)g_scope_sock + 1, &fds, NULL, NULL, &tv);

		if (ready > 0) {
			int n = recvfrom(g_scope_sock, command, sizeof command - 1, 0, NULL, NULL);
			if (n > 0) {
				command[n] = '\0';
				if (test_signal_command(command)) {
					last_command = GetTickCount();
				}
			}
		}
		else if (ready < 0) {
			Sleep(SCOPE_SEND_INTERVAL_MS);      // select itself failed. Sleep instead, so the loop can't spin
		}

		//The panel repeats its command every second. If it has gone quiet, it has been closed (or
		//has crashed), so switch the test signal off rather than leave it playing with no off switch.
		//Unsigned subtraction keeps this correct when GetTickCount wraps (every 49 days)
		if (test_signal_is_on() && GetTickCount() - last_command >= SCOPE_GEN_TIMEOUT_MS) {
			test_signal_off();
		}
	}
	return 0;
}

//cleans up the failed socket setup
static void scope_setup_failure(void) {

	if (g_scope_sock != INVALID_SOCKET) { closesocket(g_scope_sock); g_scope_sock = INVALID_SOCKET; }
	WSACleanup();
}

//Starts the sender thread
int start_scope_sender(int sample_rate) {

	if (g_scope_thread != NULL) {               // checks if the thread is running
		return 0;
	}
	if (sample_rate <= 0) {
		fprintf(stderr, "start_scope_sender: the sample rate must be greater than 0\n");
		return -1;
	}

	// Windows requires this call before any socket function.
	// each call is matched by a WSACleanup. (The UI listener makes its own pair of calls. That is fine, Windows counts them)
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		return -1;
	}
	// Create a UDP socket (SOCK_DGRAM = UDP, AF_INET = IPv4)
	g_scope_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (g_scope_sock == INVALID_SOCKET) {
		scope_setup_failure();
		return -1;
	}

	// Bind it locally (127.0.0.1), on port 0, which asks Windows to pick any free port.
	// The panel doesn't need to know the number in advance: it replies to wherever the samples came from
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(0);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(g_scope_sock, (struct sockaddr *)&addr, sizeof addr) != 0) {
		scope_setup_failure();
		return -1;
	}

	//Stop a closed panel from turning into recvfrom errors (see SIO_UDP_CONNRESET above)
	//If this fails nothing breaks, the sender just ignores the odd failed recvfrom
	BOOL report_reset = FALSE;
	DWORD bytes_returned = 0;
	WSAIoctl(g_scope_sock, SIO_UDP_CONNRESET, &report_reset, sizeof report_reset, NULL, 0, &bytes_returned, NULL, NULL);

	// The scope panel listens on the port defined in scope_tap.h, locally (127.0.0.1)
	memset(&g_scope_addr, 0, sizeof g_scope_addr);
	g_scope_addr.sin_family = AF_INET;
	g_scope_addr.sin_port = htons(SCOPE_PORT);
	g_scope_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	g_scope_sample_rate = sample_rate;
	test_signal_init(sample_rate);                                             // sets its sample rate, and the test signal always starts off

	//Start every channel empty, so nothing from a previous run is sent
	for (int ch = 0; ch < SCOPE_NUM_CHANNELS; ch++) {
		atomic_store_explicit(&g_scope_rings[ch].write_count, 0, memory_order_relaxed);
		atomic_store_explicit(&g_scope_rings[ch].read_count, 0, memory_order_relaxed);
		atomic_store_explicit(&g_scope_rings[ch].dropped, 0, memory_order_relaxed);
	}

	//Setting this flag is what lets scope_write() start filling the ring buffers
	atomic_store_explicit(&g_scope_running, 1, memory_order_release);
	g_scope_thread = CreateThread(NULL, 0, scope_sender_thread, NULL, 0, NULL);
	if (g_scope_thread == NULL) {
		atomic_store_explicit(&g_scope_running, 0, memory_order_relaxed);
		scope_setup_failure();
		return -1;
	}

	return 0;
}

//stop and clean up the thread
void stop_scope_sender(void) {

	if (g_scope_thread == NULL) {                       // if the thread ain't running, don't stop it
		return;
	}

	//Clearing the flag ends the sender loop, and makes scope_write() return straight away.
	//The ring buffers are static, so a callback that is part way through a write is still safe
	atomic_store(&g_scope_running, 0);
	test_signal_off();                                  // and the test signal fades out
	WaitForSingleObject(g_scope_thread, INFINITE);      // wait for the sender thread to exit
	CloseHandle(g_scope_thread);                        // release the thread
	g_scope_thread = NULL;
	closesocket(g_scope_sock);                          // close the socket
	g_scope_sock = INVALID_SOCKET;
	WSACleanup();                                       // call cleanup explicitly
}
