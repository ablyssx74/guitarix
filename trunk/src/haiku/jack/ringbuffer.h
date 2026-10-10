#ifndef HAIKU_JACK_RINGBUFFER_H
#define HAIKU_JACK_RINGBUFFER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _jack_ringbuffer jack_ringbuffer_t;

jack_ringbuffer_t* jack_ringbuffer_create(size_t sz);
void jack_ringbuffer_free(jack_ringbuffer_t* rb);
int jack_ringbuffer_mlock(jack_ringbuffer_t* rb);
void jack_ringbuffer_reset(jack_ringbuffer_t* rb);
size_t jack_ringbuffer_read(jack_ringbuffer_t* rb, char* dest, size_t cnt);
void jack_ringbuffer_read_advance(jack_ringbuffer_t* rb, size_t cnt);
size_t jack_ringbuffer_write(jack_ringbuffer_t* rb, const char* src, size_t cnt);
void jack_ringbuffer_write_advance(jack_ringbuffer_t* rb, size_t cnt);

#ifdef __cplusplus
}
#endif

#endif
