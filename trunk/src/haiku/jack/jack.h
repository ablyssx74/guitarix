#ifndef HAIKU_JACK_H
#define HAIKU_JACK_H

#include <jack/types.h>

#ifdef __cplusplus
extern "C" {
#endif

jack_client_t* jack_client_open(const char* client_name, jack_options_t options, jack_status_t* status, ...);
int jack_client_close(jack_client_t* client);
const char* jack_get_client_name(jack_client_t* client);
int jack_activate(jack_client_t* client);
int jack_deactivate(jack_client_t* client);

int jack_set_process_callback(jack_client_t* client, JackProcessCallback cb, void* arg);
int jack_set_xrun_callback(jack_client_t* client, JackXRunCallback cb, void* arg);
int jack_set_sample_rate_callback(jack_client_t* client, JackSampleRateCallback cb, void* arg);
int jack_set_buffer_size_callback(jack_client_t* client, JackBufferSizeCallback cb, void* arg);
int jack_set_port_registration_callback(jack_client_t* client, JackPortRegistrationCallback cb, void* arg);
int jack_set_port_connect_callback(jack_client_t* client, JackPortConnectCallback cb, void* arg);
void jack_on_shutdown(jack_client_t* client, JackShutdownCallback cb, void* arg);

jack_nframes_t jack_get_sample_rate(jack_client_t* client);
jack_nframes_t jack_get_buffer_size(jack_client_t* client);
int jack_set_buffer_size(jack_client_t* client, jack_nframes_t nframes);
int jack_is_realtime(jack_client_t* client);
int jack_client_real_time_priority(jack_client_t* client);
float jack_cpu_load(jack_client_t* client);
jack_nframes_t jack_frame_time(const jack_client_t* client);
float jack_get_xrun_delayed_usecs(jack_client_t* client);

jack_port_t* jack_port_register(jack_client_t* client, const char* port_name, const char* port_type,
	unsigned long flags, unsigned long buffer_size);
int jack_port_unregister(jack_client_t* client, jack_port_t* port);
void* jack_port_get_buffer(jack_port_t* port, jack_nframes_t nframes);
const char* jack_port_name(const jack_port_t* port);
const char** jack_port_get_connections(const jack_port_t* port);
int jack_port_connected_to(const jack_port_t* port, const char* port_name);
int jack_port_is_mine(const jack_client_t* client, const jack_port_t* port);
jack_port_t* jack_port_by_name(jack_client_t* client, const char* port_name);
jack_port_t* jack_port_by_id(jack_client_t* client, jack_port_id_t port_id);

int jack_connect(jack_client_t* client, const char* source_port, const char* destination_port);
int jack_disconnect(jack_client_t* client, const char* source_port, const char* destination_port);
const char** jack_get_ports(jack_client_t* client, const char* port_name_pattern, const char* type_name_pattern,
	unsigned long flags);
void jack_free(void* ptr);

jack_transport_state_t jack_transport_query(const jack_client_t* client, jack_position_t* pos);

#ifdef __cplusplus
}
#endif

#endif
