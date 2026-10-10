/*
 * Small Haiku-specific helpers for Guitarix (the parts GTK cannot do on its own on Haiku).
 */
#ifndef HAIKU_INTEGRATION_H
#define HAIKU_INTEGRATION_H

// Opens a web address in the user's preferred application for it (the default browser). False if that failed.
// GTK's own show_uri finds no handler on Haiku and fails silently.
bool GxHaikuOpenUrl(const char* url);

// Loads the Wayland server library (the in-process "HaiWay" server that GTK talks to) that comes with Guitarix,
// before GTK starts. The one in the system's wayland_server package cannot start a drag, so every drag and drop in
// the rack (moving an effect, dropping one from the pool) stalls with the effect gone. Our copy is newer
// (src/haiku/wayland-server). Does nothing when the copy is not there, or when GX_HAIKU_SYSTEM_WAYLAND is set.
void GxHaikuPreloadWaylandServer();

#endif
