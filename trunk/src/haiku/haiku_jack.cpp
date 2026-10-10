/*
 * The part of the JACK API that Guitarix uses, implemented on the Haiku Media Kit (through GxHaikuAudio).
 *
 * There is no JACK server here. What the engine sees:
 *   - clients and ports that are plain float buffers;
 *   - jack_connect between two of our own ports is honoured (the amp/insert split uses it); anything involving a
 *     port of another program is accepted and ignored;
 *   - the first audio input port registered is fed from the sound input, the audio output ports of the last client
 *     go to the sound output (first = left, second = right);
 *   - MIDI ports exist but carry no events yet (MIDI2 is for later);
 *   - the process callbacks of all active clients run in registration order, on the output thread of GxHaikuAudio.
 */
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include <atomic>
#include <stdio.h>
#include <OS.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "gx_haiku_audio.h"
#include "gx_haiku_midi.h"


static const uint32 kMaxFrames = 8192;


// the buffer of a MIDI port (it lives in the port's buf, which is big enough)
struct MidiBuf {
	uint32_t	count;
	struct Ev {
		jack_nframes_t	time;
		size_t			size;
		uint8_t			data[4];
	} ev[64];
};


struct _jack_port {
	jack_port_id_t		id;
	std::string			name;			// "client:port"
	jack_client_t*		client;
	unsigned long		flags;
	bool				midi;
	float*				buf;			// audio: kMaxFrames floats
	_jack_port*			source;			// inputs: the own output port feeding this one
	std::vector<_jack_port*> sinks;		// outputs: own input ports fed from this one
};


struct _jack_client {
	std::string			name;
	std::vector<_jack_port*> ports;
	JackProcessCallback	process;
	void*				processArg;
	std::atomic<bool>	active;
	JackShutdownCallback shutdown;
	void*				shutdownArg;

	_jack_client() : process(NULL), processArg(NULL), active(false), shutdown(NULL), shutdownArg(NULL) {}
};


namespace {

struct Shim {
	std::vector<_jack_client*>	clients;			// in the order they were opened
	std::vector<_jack_port*>	ports;				// index = port id
	GxHaikuAudio*				audio;
	GxHaikuMidi*				midi;
	uint32						rate;
	int32						frames;
	bool						detected;
	_jack_port*					hardwareIn;
	std::atomic<uint64_t>		frameTime;
	bool						statsStarted;

	Shim() : audio(NULL), midi(NULL), rate(48000), frames(1024), detected(false), hardwareIn(NULL), frameTime(0), statsStarted(false) {}
};

Shim gShim;


void
Detect()
{
	if (!gShim.detected) {
		GxHaikuAudio::DetectSettings(&gShim.rate, &gShim.frames);
		gShim.detected = true;
	}
}


bool
IsAudioInput(const _jack_port* p)
{
	return !p->midi && (p->flags & JackPortIsInput) != 0;
}


bool
IsAudioOutput(const _jack_port* p)
{
	return !p->midi && (p->flags & JackPortIsOutput) != 0;
}


_jack_port*
FindPort(const char* name)
{
	for (size_t i = 0; i < gShim.ports.size(); i++) {
		if (gShim.ports[i] != NULL && gShim.ports[i]->name == name)
			return gShim.ports[i];
	}
	return NULL;
}


// the last active client's first two audio outputs are what we hear
void
FindHardwareOutputs(_jack_port** left, _jack_port** right)
{
	*left = *right = NULL;
	for (size_t c = gShim.clients.size(); c > 0; c--) {
		_jack_client* client = gShim.clients[c - 1];
		if (!client->active)
			continue;
		for (size_t i = 0; i < client->ports.size(); i++) {
			_jack_port* p = client->ports[i];
			if (!IsAudioOutput(p))
				continue;
			if (*left == NULL)
				*left = p;
			else if (*right == NULL)
				*right = p;
		}
		break;
	}
}


// runs on the Media Kit output thread
void
Trampoline(uint32 n, const float* in, float* outL, float* outR, void*)
{
	if (n > kMaxFrames)
		n = kMaxFrames;

	for (size_t c = 0; c < gShim.clients.size(); c++) {
		_jack_client* client = gShim.clients[c];
		if (!client->active)
			continue;
		for (size_t i = 0; i < client->ports.size(); i++) {
			_jack_port* p = client->ports[i];
			if (p->midi && (p->flags & JackPortIsInput) != 0) {
				// the events that arrived since the last callback
				MidiBuf* mb = (MidiBuf*)p->buf;
				mb->count = 0;
				GxMidiEvent e;
				while (gShim.midi != NULL && mb->count < 64 && gShim.midi->Pop(&e)) {
					MidiBuf::Ev& ev = mb->ev[mb->count++];
					ev.time = 1;
					ev.size = e.size;
					memcpy(ev.data, e.data, 3);
				}
				continue;
			}
			if (!IsAudioInput(p))
				continue;
			if (p->source != NULL)
				memcpy(p->buf, p->source->buf, n * sizeof(float));
			else if (p == gShim.hardwareIn)
				memcpy(p->buf, in, n * sizeof(float));
			else
				memset(p->buf, 0, n * sizeof(float));
		}
		if (client->process != NULL)
			client->process(n, client->processArg);
	}

	_jack_port* left;
	_jack_port* right;
	FindHardwareOutputs(&left, &right);
	if (left != NULL)
		memcpy(outL, left->buf, n * sizeof(float));
	else
		memset(outL, 0, n * sizeof(float));
	if (right != NULL)
		memcpy(outR, right->buf, n * sizeof(float));
	else if (left != NULL)
		memcpy(outR, left->buf, n * sizeof(float));
	else
		memset(outR, 0, n * sizeof(float));

	gShim.frameTime += n;
}


// GX_HAIKU_STATS=1: print the audio statistics once a second on stderr
int32
StatsThread(void*)
{
	while (gShim.audio != NULL) {
		snooze(1000000);
		GxHaikuAudio* audio = gShim.audio;
		if (audio == NULL)
			break;
		GxHaikuAudioStats s;
		audio->GetStats(&s);
		fprintf(stderr, "[audio] %u Hz %d frames  callbacks %llu  period max %.0f us  load %.1f%%  "
			"underruns %llu  catch-ups %llu  in peak %.1f%%  midi events %llu  own latency ~%.1f ms%s\n",
			s.sampleRate, s.bufferFrames, (unsigned long long)s.callbacks, s.periodMaxUs, s.loadPercent,
			(unsigned long long)s.underruns, (unsigned long long)s.catchUps, s.inputPeak[0] * 100,
			(unsigned long long)(gShim.midi != NULL ? gShim.midi->Received() : 0), s.estimatedLatencyMs, s.realtimeConfigured ? "" : "  (real-time audio NOT configured)");
	}
	return 0;
}


bool
AnyActive()
{
	for (size_t i = 0; i < gShim.clients.size(); i++) {
		if (gShim.clients[i]->active)
			return true;
	}
	return false;
}

}	// namespace


extern "C" {

// ------------------------------------------------------------------ clients

jack_client_t*
jack_client_open(const char* name, jack_options_t, jack_status_t* status, ...)
{
	Detect();
	if (status != NULL)
		*status = (jack_status_t)0;
	_jack_client* client = new _jack_client();
	client->name = name;
	gShim.clients.push_back(client);
	return client;
}


int
jack_client_close(jack_client_t* client)
{
	if (client == NULL)
		return -1;
	jack_deactivate(client);
	for (size_t i = 0; i < client->ports.size(); i++) {
		_jack_port* p = client->ports[i];
		gShim.ports[p->id] = NULL;
		if (gShim.hardwareIn == p)
			gShim.hardwareIn = NULL;
		delete[] p->buf;
		delete p;
	}
	for (size_t i = 0; i < gShim.clients.size(); i++) {
		if (gShim.clients[i] == client) {
			gShim.clients.erase(gShim.clients.begin() + i);
			break;
		}
	}
	delete client;
	return 0;
}


const char*
jack_get_client_name(jack_client_t* client)
{
	return client->name.c_str();
}


int
jack_activate(jack_client_t* client)
{
	if (client == NULL)
		return -1;
	if (gShim.audio == NULL) {
		gShim.audio = new GxHaikuAudio();
		status_t err = gShim.audio->Start(Trampoline, NULL, gShim.rate, gShim.frames);
		if (err != B_OK) {
			delete gShim.audio;
			gShim.audio = NULL;
			return -1;
		}
	}
	client->active = true;
	if (gShim.midi == NULL) {
		for (size_t i = 0; i < client->ports.size(); i++) {
			if (client->ports[i]->midi && (client->ports[i]->flags & JackPortIsInput) != 0) {
				GxHaikuMidi* midi = new GxHaikuMidi();
				if (midi->Start() == B_OK)
					gShim.midi = midi;
				else
					delete midi;
				break;
			}
		}
	}
	if (getenv("GX_HAIKU_STATS") != NULL && !gShim.statsStarted) {
		gShim.statsStarted = true;
		resume_thread(spawn_thread(StatsThread, "gx stats", B_LOW_PRIORITY, NULL));
	}
	return 0;
}


int
jack_deactivate(jack_client_t* client)
{
	if (client == NULL)
		return -1;
	client->active = false;
	if (gShim.audio != NULL && !AnyActive()) {
		gShim.audio->Stop();
		delete gShim.audio;
		gShim.audio = NULL;
		if (gShim.midi != NULL) {
			delete gShim.midi;
			gShim.midi = NULL;
		}
	}
	return 0;
}


// ------------------------------------------------------------------ callbacks

int
jack_set_process_callback(jack_client_t* client, JackProcessCallback cb, void* arg)
{
	client->process = cb;
	client->processArg = arg;
	return 0;
}


// the rest are accepted and never called: nothing changes under us (rate and buffer size are fixed for the session)
int jack_set_xrun_callback(jack_client_t*, JackXRunCallback, void*) { return 0; }
int jack_set_sample_rate_callback(jack_client_t*, JackSampleRateCallback, void*) { return 0; }
int jack_set_buffer_size_callback(jack_client_t*, JackBufferSizeCallback, void*) { return 0; }
int jack_set_port_registration_callback(jack_client_t*, JackPortRegistrationCallback, void*) { return 0; }
int jack_set_port_connect_callback(jack_client_t*, JackPortConnectCallback, void*) { return 0; }


void
jack_on_shutdown(jack_client_t* client, JackShutdownCallback cb, void* arg)
{
	client->shutdown = cb;
	client->shutdownArg = arg;
}


// ------------------------------------------------------------------ server information

jack_nframes_t jack_get_sample_rate(jack_client_t*) { Detect(); return gShim.rate; }
jack_nframes_t jack_get_buffer_size(jack_client_t*) { Detect(); return (jack_nframes_t)gShim.frames; }
int jack_set_buffer_size(jack_client_t*, jack_nframes_t) { return -1; }	// set it in the Haiku sound driver settings
int jack_is_realtime(jack_client_t*) { return 0; }		// our threads get Haiku real-time priority themselves
int jack_client_real_time_priority(jack_client_t*) { return 0; }
jack_nframes_t jack_frame_time(const jack_client_t*) { return (jack_nframes_t)gShim.frameTime.load(); }
float jack_get_xrun_delayed_usecs(jack_client_t*) { return 0.0f; }


float
jack_cpu_load(jack_client_t*)
{
	if (gShim.audio == NULL)
		return 0.0f;
	GxHaikuAudioStats s;
	gShim.audio->GetStats(&s);
	return (float)s.loadPercent;
}


jack_transport_state_t
jack_transport_query(const jack_client_t*, jack_position_t* pos)
{
	if (pos != NULL)
		memset(pos, 0, sizeof(*pos));
	return JackTransportStopped;
}


// ------------------------------------------------------------------ ports

jack_port_t*
jack_port_register(jack_client_t* client, const char* name, const char* type, unsigned long flags, unsigned long)
{
	_jack_port* p = new _jack_port();
	p->id = (jack_port_id_t)gShim.ports.size();
	p->name = client->name + ":" + name;
	p->client = client;
	p->flags = flags;
	p->midi = strcmp(type, JACK_DEFAULT_MIDI_TYPE) == 0;
	p->buf = new float[kMaxFrames];
	memset(p->buf, 0, kMaxFrames * sizeof(float));
	p->source = NULL;
	gShim.ports.push_back(p);
	client->ports.push_back(p);
	if (gShim.hardwareIn == NULL && IsAudioInput(p))
		gShim.hardwareIn = p;
	return p;
}


int
jack_port_unregister(jack_client_t* client, jack_port_t* port)
{
	if (client == NULL || port == NULL)
		return -1;
	for (size_t i = 0; i < client->ports.size(); i++) {
		if (client->ports[i] == port) {
			client->ports.erase(client->ports.begin() + i);
			break;
		}
	}
	for (size_t i = 0; i < gShim.ports.size(); i++) {
		_jack_port* o = gShim.ports[i];
		if (o == NULL)
			continue;
		if (o->source == port)
			o->source = NULL;
		for (size_t k = 0; k < o->sinks.size(); k++) {
			if (o->sinks[k] == port) {
				o->sinks.erase(o->sinks.begin() + k);
				break;
			}
		}
	}
	gShim.ports[port->id] = NULL;
	if (gShim.hardwareIn == port)
		gShim.hardwareIn = NULL;
	delete[] port->buf;
	delete port;
	return 0;
}


void*
jack_port_get_buffer(jack_port_t* port, jack_nframes_t)
{
	return port->buf;
}


const char* jack_port_name(const jack_port_t* port) { return port->name.c_str(); }
int jack_port_is_mine(const jack_client_t* client, const jack_port_t* port) { return port->client == client; }

jack_port_t*
jack_port_by_name(jack_client_t*, const char* name)
{
	return FindPort(name);
}


jack_port_t*
jack_port_by_id(jack_client_t*, jack_port_id_t id)
{
	return id < gShim.ports.size() ? gShim.ports[id] : NULL;
}


const char**
jack_port_get_connections(const jack_port_t* port)
{
	std::vector<std::string> names;
	if (port->source != NULL)
		names.push_back(port->source->name);
	for (size_t i = 0; i < port->sinks.size(); i++)
		names.push_back(port->sinks[i]->name);
	// the sound input/output the engine is wired to
	if (port == gShim.hardwareIn)
		names.push_back("system:capture_1");
	if (IsAudioOutput(port) && port->sinks.empty()) {
		_jack_port* left;
		_jack_port* right;
		FindHardwareOutputs(&left, &right);
		if (port == left)
			names.push_back("system:playback_1");
		else if (port == right)
			names.push_back("system:playback_2");
	}
	if (names.empty())
		return NULL;
	const char** list = (const char**)malloc((names.size() + 1) * sizeof(char*));
	for (size_t i = 0; i < names.size(); i++)
		list[i] = strdup(names[i].c_str());
	list[names.size()] = NULL;
	return list;
}


int
jack_port_connected_to(const jack_port_t* port, const char* name)
{
	if (port->source != NULL && port->source->name == name)
		return 1;
	for (size_t i = 0; i < port->sinks.size(); i++) {
		if (port->sinks[i]->name == name)
			return 1;
	}
	return 0;
}


int
jack_connect(jack_client_t*, const char* from, const char* to)
{
	_jack_port* a = FindPort(from);
	_jack_port* b = FindPort(to);
	if (a != NULL && b != NULL && IsAudioOutput(a) && IsAudioInput(b)) {
		b->source = a;
		a->sinks.push_back(b);
	}
	return 0;		// connections to other programs are accepted and ignored
}


int
jack_disconnect(jack_client_t*, const char* from, const char* to)
{
	_jack_port* a = FindPort(from);
	_jack_port* b = FindPort(to);
	if (a != NULL && b != NULL && b->source == a) {
		b->source = NULL;
		for (size_t i = 0; i < a->sinks.size(); i++) {
			if (a->sinks[i] == b) {
				a->sinks.erase(a->sinks.begin() + i);
				break;
			}
		}
	}
	return 0;
}


const char**
jack_get_ports(jack_client_t*, const char*, const char*, unsigned long)
{
	return NULL;		// no other JACK clients on Haiku
}


void
jack_free(void* ptr)
{
	free(ptr);
}


// ------------------------------------------------------------------ MIDI (ports exist, no events yet)

uint32_t
jack_midi_get_event_count(void* port_buffer)
{
	return ((MidiBuf*)port_buffer)->count;
}


int
jack_midi_event_get(jack_midi_event_t* event, void* port_buffer, uint32_t index)
{
	MidiBuf* mb = (MidiBuf*)port_buffer;
	if (index >= mb->count)
		return -1;
	event->time = mb->ev[index].time;
	event->size = mb->ev[index].size;
	event->buffer = mb->ev[index].data;
	return 0;
}


void
jack_midi_clear_buffer(void* port_buffer)
{
	((MidiBuf*)port_buffer)->count = 0;
}


jack_midi_data_t*
jack_midi_event_reserve(void* port_buffer, jack_nframes_t time, size_t size)
{
	MidiBuf* mb = (MidiBuf*)port_buffer;
	if (mb->count >= 64 || size > sizeof(mb->ev[0].data))
		return NULL;
	MidiBuf::Ev& ev = mb->ev[mb->count++];
	ev.time = time;
	ev.size = size;
	return ev.data;
}


int
jack_midi_event_write(void* port_buffer, jack_nframes_t time, const jack_midi_data_t* data, size_t size)
{
	jack_midi_data_t* p = jack_midi_event_reserve(port_buffer, time, size);
	if (p == NULL)
		return -1;
	memcpy(p, data, size);
	return 0;
}


// ------------------------------------------------------------------ ring buffer

struct _jack_ringbuffer {
	char*				buf;
	size_t				size;
	std::atomic<size_t>	rp;
	std::atomic<size_t>	wp;
};


jack_ringbuffer_t*
jack_ringbuffer_create(size_t sz)
{
	jack_ringbuffer_t* rb = new jack_ringbuffer_t();
	rb->size = sz + 1;
	rb->buf = new char[rb->size];
	rb->rp = rb->wp = 0;
	return rb;
}


void
jack_ringbuffer_free(jack_ringbuffer_t* rb)
{
	if (rb != NULL) {
		delete[] rb->buf;
		delete rb;
	}
}


int jack_ringbuffer_mlock(jack_ringbuffer_t*) { return 0; }
void jack_ringbuffer_reset(jack_ringbuffer_t* rb) { rb->rp = rb->wp = 0; }


size_t
jack_ringbuffer_read(jack_ringbuffer_t* rb, char* dest, size_t cnt)
{
	size_t r = rb->rp.load(std::memory_order_relaxed);
	size_t w = rb->wp.load(std::memory_order_acquire);
	size_t avail = (w + rb->size - r) % rb->size;
	if (cnt > avail)
		cnt = avail;
	for (size_t i = 0; i < cnt; i++)
		dest[i] = rb->buf[(r + i) % rb->size];
	rb->rp.store((r + cnt) % rb->size, std::memory_order_release);
	return cnt;
}


void jack_ringbuffer_read_advance(jack_ringbuffer_t*, size_t) {}	// jack_ringbuffer_read already advanced


size_t
jack_ringbuffer_write(jack_ringbuffer_t* rb, const char* src, size_t cnt)
{
	size_t w = rb->wp.load(std::memory_order_relaxed);
	size_t r = rb->rp.load(std::memory_order_acquire);
	size_t space = (r + rb->size - w - 1) % rb->size;
	if (cnt > space)
		cnt = space;
	for (size_t i = 0; i < cnt; i++)
		rb->buf[(w + i) % rb->size] = src[i];
	rb->wp.store((w + cnt) % rb->size, std::memory_order_release);
	return cnt;
}


void jack_ringbuffer_write_advance(jack_ringbuffer_t*, size_t) {}	// jack_ringbuffer_write already advanced

}	// extern "C"
