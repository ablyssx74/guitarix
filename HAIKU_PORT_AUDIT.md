# Guitarix on Haiku: phase 1 audit

## Engine and JACK
- JACK lives in `trunk/src/gx_head/engine/gx_jack.cpp` (1182 lines), `trunk/src/headers/gx_jack.h`, and the GUI's `gx_portmap.cpp` (669 lines) and `gx_jack_options.cpp`.
- The code uses about 60 distinct `jack_*` calls. The ones that matter for audio:
  - `jack_client_open`, `jack_set_process_callback`, `jack_activate`, `jack_deactivate`, `jack_client_close`
  - `jack_port_register`, `jack_port_get_buffer`
  - the sample-rate, buffer-size and xrun callbacks
  - `jack_midi_*` for MIDI in and out
- The rest is JACK-only and can be a stub or hidden:
  - port connections and the port map window (`jack_connect`, `jack_get_ports`, ...)
  - JACK session, transport, the ringbuffer and RT priority
  - `jack_capture` (an external program) and the Meterbridge child process
- Process entry points: `GxJack::gx_jack_process` (mono, calls `engine.mono_chain.process(n, in, out)`) and `gx_jack_insert_process` (stereo, `stereo_chain`). Both are plain buffer-in, buffer-out calls into the engine.
- **Existing seam.** `GUITARIX_AS_PLUGIN` builds the same engine with *no JACK*. It uses `gx_jack_wrapper.h/.cpp`, a `GxJack` facade with `process_mono`, `process_stereo`, `srate_callback`, `buffersize_callback` and `finish_process`. A Haiku backend can be a third implementation of that same facade, which is far smaller than shimming libjack.
- `GxJack` is used outside the JACK files in:
  - `gx_preset.cpp` (reads and writes the saved connections)
  - `gx_paramtable.cpp` and `gx_internal_ui_plugins.cpp` (CPU load, buffer size, sample rate)
  - `machine.cpp`
  - `gx_main_window.cpp` (the latency menu)
  - `gx_child_process.cpp`
  - `jsonrpc.*`

## Dependencies (waf `wscript`) against HaikuPorts on the VM (hrev60228)
| Need | Haiku |
|---|---|
| gtk+-3.0 >= 3.22 | gtk3 3.24.52 |
| gtkmm-3.0 >= 3.22, glibmm >= 2.56 | gtkmm3 3.24.11, glibmm 2.66.2 |
| sigc++ 2 | libsigc++ 2.9.3 |
| boost >= 1.42 (system, iostreams, thread) | boost1.83 |
| sndfile, fftw3f | libsndfile 1.2.2, fftw 3.3.10 |
| lilv-0, lv2 | lilv 0.24.20, lv2 1.18.10 |
| lrdf | liblrdf 0.6.1 |
| ladspa.h | ladspa_sdk (falls back to a bundled copy) |
| libcurl | curl 8.22 |
| zita-resampler, zita-convolver | bundled in the source tree |
| liblo (NSM) | liblo 0.31 (optional) |
| avahi, bluez | not on Haiku (optional features, off) |
| eigen | eigen3 3.4 / eigen 5 (for NAM / RTNeural) |
| gperf | gperf 3.1 |
| JACK | none, replaced by the Haiku backend |

All required dependencies are available. The GTK UI is GTK3 (not GTK2), which HaikuPorts has.

## Haiku audio and MIDI
Headers present: `SoundPlayer.h`, `MediaRecorder.h`, `Sound.h`, `Midi2Defs.h`, `MidiRoster.h`, `MidiProducer.h`, `MidiConsumer.h`.

## Risks
1. **Latency** of `BMediaRecorder` plus `BSoundPlayer` on Haiku. Measure this first.
2. **Guitarix is large.** It includes the NAM and RTNeural neural-amp code, which uses heavy templates and SIMD. It should be fine on x86_64, so this port is x86_64 only. 32-bit gcc2 is not possible (GTK3 and modern C++).
3. **GTK3 on Haiku** is a port with its own quirks, so the custom Gxw widgets need testing.
4. **waf** needs a Haiku tweak, e.g. library paths and `-lnetwork`.

## Proposed phase 2
1. `gx_jack_haiku.cpp` implements the same facade as `gx_jack_wrapper`: open `BSoundPlayer` / `BMediaRecorder`, a ring buffer between them, and call `process_mono` / `process_stereo` with fixed-size blocks.
2. Build only the engine plus a small command-line test program (no GTK) to measure latency on the VM.
3. Stub the JACK port APIs in the GUI (port map, connections) and hide those menus.
