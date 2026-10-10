/*
 * MIDI input for Guitarix on Haiku (MIDI2 kit).
 *
 * Registers a "Guitarix" consumer endpoint with the Midi Roster (with the program's icon, so the patchbay shows it),
 * connects it to the MIDI producers that exist and to the ones that appear later, and hands the incoming events to the
 * audio thread through a lock-free queue.
 */
#ifndef GX_HAIKU_MIDI_H
#define GX_HAIKU_MIDI_H

#include <SupportDefs.h>
#include <atomic>
#include <stdint.h>


struct GxMidiEvent {
	uint8_t		size;
	uint8_t		data[3];
};


class GxHaikuMidi {
public:
						GxHaikuMidi();
						~GxHaikuMidi();

	// autoConnect: connect to every MIDI producer (set GX_HAIKU_MIDI=off to leave the connecting to the patchbay)
			status_t	Start();
			void		Stop();

	// audio thread: next event, false when there is none
			bool		Pop(GxMidiEvent* event);

			// called by the endpoint (its own thread)
			void		Push(const uint8_t* data, size_t length);
			uint64_t	Received() const { return fReceived.load(); }

			class Input;
			class Watcher;
private:
			void		_ConnectProducer(int32 id);

			Input*		fInput;
			Watcher*	fWatcher;
			bool		fAutoConnect;

			static const uint32_t kSize = 512;		// power of two
			GxMidiEvent	fQueue[kSize];
			std::atomic<uint32_t>	fRead;
			std::atomic<uint32_t>	fWrite;
			std::atomic<uint64_t>	fReceived;
};


#endif
