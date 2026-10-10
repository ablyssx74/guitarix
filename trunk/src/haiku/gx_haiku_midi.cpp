#include "gx_haiku_midi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Application.h>
#include <File.h>
#include <Looper.h>
#include <Message.h>
#include <MidiConsumer.h>
#include <MidiEndpoint.h>
#include <MidiProducer.h>
#include <MidiRoster.h>
#include <OS.h>
#include <Resources.h>
#include <Roster.h>


static const char* kEndpointName = "Guitarix";


// the program's own vector icon, from the resources of its executable
static void
SetEndpointIcon(BMidiEndpoint* endpoint)
{
	BMessage properties;
	if (endpoint->GetProperties(&properties) != B_OK)
		return;

	// the executable of this team, found without needing a BApplication
	image_info image;
	int32 cookie = 0;
	bool found = false;
	while (get_next_image_info(0, &cookie, &image) == B_OK) {
		if (image.type == B_APP_IMAGE) {
			found = true;
			break;
		}
	}
	if (!found)
		return;

	BFile file(image.name, B_READ_ONLY);
	BResources resources;
	if (file.InitCheck() != B_OK || resources.SetTo(&file) != B_OK)
		return;

	size_t size = 0;
	const void* data = resources.LoadResource(B_VECTOR_ICON_TYPE, "BEOS:ICON", &size);
	if (data == NULL || size == 0)
		return;

	properties.RemoveName("icon");
	properties.AddData("icon", B_VECTOR_ICON_TYPE, data, size);
	endpoint->SetProperties(&properties);
}


// ------------------------------------------------------------------ the consumer endpoint

class GxHaikuMidi::Input : public BMidiLocalConsumer {
public:
	Input(GxHaikuMidi* owner) : BMidiLocalConsumer(kEndpointName), fOwner(owner) {}

	// all messages arrive here as raw bytes (the default would split them into NoteOn() and friends)
	virtual void Data(uchar* data, size_t length, bool, bigtime_t)
	{
		size_t i = 0;
		while (i < length) {
			uint8_t status = data[i];
			if (status < 0x80) {			// stray data byte
				i++;
				continue;
			}
			size_t n;
			if (status >= 0xf8)
				n = 1;						// real time: clock, start, stop...
			else if (status == 0xf0) {		// system exclusive: skipped
				while (i < length && data[i] != 0xf7)
					i++;
				i++;
				continue;
			} else if (status == 0xf1 || status == 0xf3)
				n = 2;
			else if (status == 0xf2)
				n = 3;
			else if (status >= 0xf4)
				n = 1;
			else if ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0)
				n = 2;
			else
				n = 3;
			if (i + n > length)
				break;
			fOwner->Push(data + i, n);
			i += n;
		}
	}

protected:
	~Input() {}

private:
	GxHaikuMidi*	fOwner;
};


// ------------------------------------------------------------------ hot-plug

class GxHaikuMidi::Watcher : public BLooper {
public:
	Watcher(GxHaikuMidi* owner) : BLooper("Guitarix MIDI watcher"), fOwner(owner) {}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != B_MIDI_EVENT) {
			BLooper::MessageReceived(message);
			return;
		}
		int32 op = 0;
		int32 id = 0;
		const char* type = NULL;
		if (message->FindInt32("be:op", &op) == B_OK && op == B_MIDI_REGISTERED
				&& message->FindInt32("be:id", &id) == B_OK
				&& message->FindString("be:type", &type) == B_OK && strcmp(type, "producer") == 0)
			fOwner->_ConnectProducer(id);
	}

private:
	GxHaikuMidi*	fOwner;
};


// ------------------------------------------------------------------ GxHaikuMidi

GxHaikuMidi::GxHaikuMidi()
	:
	fInput(NULL),
	fWatcher(NULL),
	fAutoConnect(true),
	fRead(0),
	fWrite(0),
	fReceived(0)
{
}


GxHaikuMidi::~GxHaikuMidi()
{
	Stop();
}


status_t
GxHaikuMidi::Start()
{
	if (fInput != NULL)
		return B_OK;
	const char* mode = getenv("GX_HAIKU_MIDI");
	fAutoConnect = !(mode != NULL && strcmp(mode, "off") == 0);

	fInput = new Input(this);
	SetEndpointIcon(fInput);
	status_t err = fInput->Register();
	if (err != B_OK) {
		fInput->Release();
		fInput = NULL;
		return err;
	}

	if (fAutoConnect) {
		fWatcher = new Watcher(this);
		fWatcher->Run();
		BMessenger messenger(NULL, fWatcher);
		BMidiRoster::StartWatching(&messenger);

		int32 id = 0;
		BMidiProducer* producer;
		while ((producer = BMidiRoster::NextProducer(&id)) != NULL) {
			_ConnectProducer(producer->ID());
			producer->Release();
		}
	}
	return B_OK;
}


void
GxHaikuMidi::Stop()
{
	if (fWatcher != NULL) {
		BMidiRoster::StopWatching();
		if (fWatcher->Lock())
			fWatcher->Quit();
		fWatcher = NULL;
	}
	if (fInput != NULL) {
		fInput->Unregister();
		fInput->Release();
		fInput = NULL;
	}
}


void
GxHaikuMidi::_ConnectProducer(int32 id)
{
	if (fInput == NULL)
		return;
	BMidiProducer* producer = BMidiRoster::FindProducer(id);
	if (producer == NULL)
		return;
	// not our own endpoints, not what is already connected
	if (strncmp(producer->Name(), kEndpointName, strlen(kEndpointName)) != 0 && !producer->IsConnected(fInput))
		producer->Connect(fInput);
	producer->Release();
}


void
GxHaikuMidi::Push(const uint8_t* data, size_t length)
{
	if (length == 0 || length > 3)
		return;
	uint32_t w = fWrite.load(std::memory_order_relaxed);
	if (w - fRead.load(std::memory_order_acquire) >= kSize)
		return;				// the audio thread is not keeping up: drop
	GxMidiEvent& e = fQueue[w & (kSize - 1)];
	e.size = (uint8_t)length;
	memset(e.data, 0, sizeof(e.data));
	memcpy(e.data, data, length);
	fWrite.store(w + 1, std::memory_order_release);
	fReceived++;
}


bool
GxHaikuMidi::Pop(GxMidiEvent* event)
{
	uint32_t r = fRead.load(std::memory_order_relaxed);
	if (r == fWrite.load(std::memory_order_acquire))
		return false;
	*event = fQueue[r & (kSize - 1)];
	fRead.store(r + 1, std::memory_order_release);
	return true;
}
