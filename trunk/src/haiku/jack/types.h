/*
 * Minimal JACK API types for the Haiku port. The real JACK is not used on Haiku: haiku_jack.cpp implements the part of
 * the API Guitarix needs on top of the Media Kit.
 */
#ifndef HAIKU_JACK_TYPES_H
#define HAIKU_JACK_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t jack_nframes_t;
typedef uint64_t jack_time_t;
typedef uint32_t jack_port_id_t;
typedef uint32_t jack_intclient_t;

typedef struct _jack_port jack_port_t;
typedef struct _jack_client jack_client_t;

#define JACK_DEFAULT_AUDIO_TYPE "32 bit float mono audio"
#define JACK_DEFAULT_MIDI_TYPE "8 bit raw midi"
#define JACK_MAX_FRAMES (4294967295U)

enum JackOptions {
	JackNullOption = 0x00,
	JackNoStartServer = 0x01,
	JackUseExactName = 0x02,
	JackServerName = 0x04,
	JackLoadName = 0x08,
	JackLoadInit = 0x10,
	JackSessionID = 0x20
};
typedef enum JackOptions jack_options_t;

enum JackStatus {
	JackFailure = 0x01,
	JackInvalidOption = 0x02,
	JackNameNotUnique = 0x04,
	JackServerStarted = 0x08,
	JackServerFailed = 0x10,
	JackServerError = 0x20,
	JackNoSuchClient = 0x40,
	JackLoadFailure = 0x80,
	JackInitFailure = 0x100,
	JackShmFailure = 0x200,
	JackVersionError = 0x400,
	JackBackendError = 0x800,
	JackClientZombie = 0x1000
};
typedef enum JackStatus jack_status_t;

enum JackPortFlags {
	JackPortIsInput = 0x1,
	JackPortIsOutput = 0x2,
	JackPortIsPhysical = 0x4,
	JackPortCanMonitor = 0x8,
	JackPortIsTerminal = 0x10
};

typedef enum {
	JackTransportStopped = 0,
	JackTransportRolling = 1,
	JackTransportLooping = 2,
	JackTransportStarting = 3,
	JackTransportNetStarting = 4
} jack_transport_state_t;

typedef uint64_t jack_unique_t;
typedef enum { JackPositionBBT = 0x10 } jack_position_bits_t;

typedef struct {
	jack_unique_t unique_1;
	jack_time_t usecs;
	jack_nframes_t frame_rate;
	jack_nframes_t frame;
	jack_position_bits_t valid;
	int32_t bar;
	int32_t beat;
	int32_t tick;
	double bar_start_tick;
	float beats_per_bar;
	float beat_type;
	double ticks_per_beat;
	double beats_per_minute;
	double frame_time;
	double next_time;
	jack_nframes_t bbt_offset;
	float audio_frames_per_video_frame;
	jack_nframes_t video_offset;
	int32_t padding[7];
	jack_unique_t unique_2;
} jack_position_t;

typedef int (*JackProcessCallback)(jack_nframes_t nframes, void* arg);
typedef int (*JackXRunCallback)(void* arg);
typedef int (*JackSampleRateCallback)(jack_nframes_t nframes, void* arg);
typedef int (*JackBufferSizeCallback)(jack_nframes_t nframes, void* arg);
typedef void (*JackPortRegistrationCallback)(jack_port_id_t port, int registered, void* arg);
typedef void (*JackPortConnectCallback)(jack_port_id_t a, jack_port_id_t b, int connect, void* arg);
typedef void (*JackShutdownCallback)(void* arg);

#ifdef __cplusplus
}
#endif

#endif
