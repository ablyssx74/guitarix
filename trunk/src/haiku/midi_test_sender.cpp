/*
 * Sends MIDI to whatever is connected to it, to try the MIDI input of the Haiku Guitarix without a controller:
 *   midi_test_sender [seconds]
 * It registers a producer "Test sender" and sends a program change, a few controller changes and notes once a second.
 * Connect it to "Guitarix" in the patchbay (Guitarix connects to new producers by itself).
 */
#include <Application.h>
#include <MidiProducer.h>
#include <MidiRoster.h>
#include <OS.h>
#include <stdio.h>
#include <stdlib.h>

class App : public BApplication {
public:
	App() : BApplication("application/x-vnd.guitarix-midi-test-sender") {}
};

int
main(int argc, char** argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 10;
	App app;
	BMidiLocalProducer* producer = new BMidiLocalProducer("Test sender");
	if (producer->Register() != B_OK) {
		printf("cannot register the producer\n");
		return 1;
	}
	printf("Producer registered, sending for %d s\n", seconds);
	snooze(1000000);			// give Guitarix time to connect
	for (int i = 0; i < seconds; i++) {
		producer->SprayProgramChange(0, i % 4);
		producer->SprayControlChange(0, 7, (i * 20) % 128);
		producer->SprayNoteOn(0, 60 + i, 100);
		snooze(100000);
		producer->SprayNoteOff(0, 60 + i, 0);
		snooze(900000);
	}
	producer->Unregister();
	producer->Release();
	return 0;
}
