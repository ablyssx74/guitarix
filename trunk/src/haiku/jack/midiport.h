#ifndef HAIKU_JACK_MIDIPORT_H
#define HAIKU_JACK_MIDIPORT_H

#include <jack/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char jack_midi_data_t;

typedef struct _jack_midi_event {
	jack_nframes_t time;
	size_t size;
	jack_midi_data_t* buffer;
} jack_midi_event_t;

uint32_t jack_midi_get_event_count(void* port_buffer);
int jack_midi_event_get(jack_midi_event_t* event, void* port_buffer, uint32_t event_index);
void jack_midi_clear_buffer(void* port_buffer);
jack_midi_data_t* jack_midi_event_reserve(void* port_buffer, jack_nframes_t time, size_t data_size);
int jack_midi_event_write(void* port_buffer, jack_nframes_t time, const jack_midi_data_t* data, size_t data_size);

#ifdef __cplusplus
}
#endif

#endif
