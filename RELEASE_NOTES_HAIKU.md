# Guitarix for Haiku 0.47.0-2

Drag and drop works in the rack, and the Help links open your browser.

## What's new
- **Drag and drop of effects works.** Moving an effect in the rack with its handle (the round screw at the corner of its box), and dragging an effect from the plugin pool into the rack, now work. Before, the effect vanished when you started the drag and stayed gone until you changed something or restarted. The cause was the Wayland server that Haiku's GTK talks to: the one in HaikuPorts' `wayland_server` package (2025-10-04) cannot start a drag. Guitarix now brings a newer copy of that server (from https://github.com/X547/wayland-server, MIT licensed, with one fix for a loop that fired thousands of motion events a second during a drag) and loads it before GTK starts. The system's own copy is not touched. Set `GX_HAIKU_SYSTEM_WAYLAND=1` to use the system's server instead.
- **The Help menu opens your browser.** **Help**, **Get NAM Models** and **Get RTNeural Models** open the default web browser. They did nothing before.

## Known limits
- No picture follows the pointer while you drag; the orange line in the rack shows where the effect will land.
- The effect has to be dropped on the rack, at the spot where the line shows.
- The MIDI input has been tried with a software sender; a real controller is still to be confirmed.
- Only the Media Kit's default sound input and output are used.

## Install
`guitarix-0.47.0-2-x86_64.hpkg` is for 64-bit Haiku. Install it **for the whole system**, not for "home". The program looks for its data and the Wayland server in the system location, so a home install cannot find them.

---

# Guitarix for Haiku 0.47.0-1 (earlier release)

A port of Guitarix 0.47.0, the virtual guitar amplifier and effects processor, to Haiku. It plays and records through the Haiku **Media Kit** and takes MIDI through the **MIDI2 Kit**; there is no JACK.

## What works
- **Audio.** The guitar comes in on the sound input (the first channel of a stereo input; a guitar on the left line-in is the usual case) and goes out to the speakers through the engine's amps, cabinets, effects, neural amp models and convolver. Plugins can be added and removed by double-clicking them (drag and drop between lists is not available). Presets, banks, the tuner, the rack and the Live Play view are the ones Guitarix has everywhere.
- **Real-time audio.** Guitarix works best when the sound driver has real-time audio configured (a small play and record buffer). If it is not, Guitarix starts anyway and shows a message once, with a "Don't remind me again" button. On a 48 kHz sound card with 128-frame buffers it ran at about 19% load with 2.7 ms between callbacks, no dropouts, and about 7.5 ms of buffering of its own; the driver and the converters add to that.
- **MIDI.** Guitarix shows up as a **Guitarix** MIDI input (with its icon) in the MIDI patchbay. It connects itself to the MIDI devices that are there and to the ones plugged in later. Program changes load presets, controller changes move the controls you have mapped, notes can be mapped, and MIDI clock sets the tempo. Set `GX_HAIKU_MIDI=off` to leave the connecting to the patchbay.
- **Icon.** An electric guitar for the program and its MIDI input.

## Differences from Guitarix on Linux
- No JACK: the Jack startup, connection, ports, latency and Meterbridge menu entries are hidden. The buffer size is the one set in the Haiku sound driver settings.
- MIDI input only; Guitarix sends no MIDI.
- The 64-bit Haiku (x86_64) only: Guitarix and GTK 3 need a modern C++ compiler, so there is no 32-bit package.
- `GX_HAIKU_STATS=1`, set before starting Guitarix from a terminal, prints a line every second with the callbacks, dropouts, load, input level and MIDI events.

## Known limits
- The MIDI input has been tried with a software sender; a real controller is still to be confirmed.
- Only the Media Kit's default sound input and output are used.

## Install
`guitarix-0.47.0-1-x86_64.hpkg` is for 64-bit Haiku. Install it **for the whole system**, not for "home". The program looks for its data in the system location, so a home install cannot find it.

## About this build
Based on the Guitarix 0.47.0 source (https://github.com/brummer10/guitarix), kept in the fork https://github.com/ablyssx74/guitarix on the `haiku-port` branch. The Haiku parts are in `trunk/src/haiku/`: the Media Kit audio backend, a small layer that stands in for the JACK calls the engine makes, the MIDI2 input, the icon, and `make-package.sh`, which builds the package.
