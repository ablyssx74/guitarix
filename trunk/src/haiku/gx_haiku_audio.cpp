#include "gx_haiku_audio.h"

#include <atomic>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include <Buffer.h>
#include <BufferConsumer.h>
#include <FindDirectory.h>
#include <MediaEventLooper.h>
#include <MediaRoster.h>
#include <OS.h>
#include <Path.h>
#include <SoundPlayer.h>
#include <TimeSource.h>

#if defined(__SSE__)
#	include <xmmintrin.h>
#endif


static const int kPeakChannels = 8;
static const int32 kMaxChunk = 8192;				// frames handled in one pass
static const int32 kDefaultFrames48k = 1024;		// the buffer used when no real-time config is found


// ------------------------------------------------------------------ single producer / single consumer ring

struct GxHaikuAudio::Ring {
	float*					buf;
	uint32_t				size;			// power of two
	uint32_t				mask;
	std::atomic<uint32_t>	w;
	std::atomic<uint32_t>	r;
	std::atomic<uint64_t>	overrunFrames;
	std::atomic<float>		peak[kPeakChannels];		// loudest sample per input channel since last read

	explicit Ring(uint32_t minSize)
		:
		w(0), r(0), overrunFrames(0)
	{
		for (int i = 0; i < kPeakChannels; i++)
			peak[i] = 0.0f;
		size = 1024;
		while (size < minSize)
			size <<= 1;
		mask = size - 1;
		buf = new float[size];
		memset(buf, 0, size * sizeof(float));
	}

	~Ring() { delete[] buf; }

	uint32_t Available() const
	{
		return w.load(std::memory_order_acquire) - r.load(std::memory_order_acquire);
	}

	// producer: drops what does not fit
	void Write(const float* data, uint32_t n)
	{
		uint32_t wp = w.load(std::memory_order_relaxed);
		uint32_t free_ = size - (wp - r.load(std::memory_order_acquire));
		if (n > free_) {
			overrunFrames += n - free_;
			n = free_;
		}
		for (uint32_t i = 0; i < n; i++)
			buf[(wp + i) & mask] = data[i];
		w.store(wp + n, std::memory_order_release);
	}

	// consumer
	void Read(float* dst, uint32_t n)
	{
		uint32_t rp = r.load(std::memory_order_relaxed);
		for (uint32_t i = 0; i < n; i++)
			dst[i] = buf[(rp + i) & mask];
		r.store(rp + n, std::memory_order_release);
	}

	// consumer: drop everything older than the newest `keep` frames
	void KeepNewest(uint32_t keep)
	{
		uint32_t avail = Available();
		if (avail > keep)
			r.store(r.load(std::memory_order_relaxed) + (avail - keep), std::memory_order_release);
	}
};


static inline float
ReadSample(const uint8* raw, uint32 format, size_t idx)
{
	switch (format) {
		case media_raw_audio_format::B_AUDIO_FLOAT:
			return ((const float*)raw)[idx];
		case media_raw_audio_format::B_AUDIO_INT:
			return (float)((const int32*)raw)[idx] * (1.0f / 2147483648.0f);
		case media_raw_audio_format::B_AUDIO_SHORT:
			return (float)((const int16*)raw)[idx] * (1.0f / 32768.0f);
		default:
			return ((float)raw[idx] - 128.0f) * (1.0f / 128.0f);
	}
}


// ------------------------------------------------------------------ input node

class GxHaikuAudio::InputNode : public BBufferConsumer, public BMediaEventLooper {
public:
	InputNode(Ring* ring, int32 channel)
		:
		BMediaNode("Guitarix-In"),
		BBufferConsumer(B_MEDIA_RAW_AUDIO),
		BMediaEventLooper(),
		fRing(ring),
		fChannel(channel),
		fFormat(0),
		fChannels(2)
	{
		AddNodeKind(B_BUFFER_CONSUMER | B_PHYSICAL_INPUT);
	}

	virtual ~InputNode() { BMediaEventLooper::Quit(); }

	virtual status_t HandleMessage(int32 code, const void* data, size_t size)
	{
		if (BBufferConsumer::HandleMessage(code, data, size) == B_OK)
			return B_OK;
		if (BMediaEventLooper::HandleMessage(code, data, size) == B_OK)
			return B_OK;
		return BMediaNode::HandleMessage(code, data, size);
	}

	virtual status_t GetLatencyFor(const media_destination&, bigtime_t* out_latency, media_node_id* out_timesource)
	{
		*out_latency = 1000;
		*out_timesource = TimeSource() ? TimeSource()->ID() : 0;
		return B_OK;
	}

	virtual BMediaNode::run_mode RunMode() { return B_RECORDING; }

	virtual void NodeRegistered()
	{
		Run();
		set_thread_priority(ControlThread(), B_REAL_TIME_PRIORITY);
	}

	virtual void HandleEvent(const media_timed_event*, bigtime_t, bool = false) {}
	virtual port_id ControlPort() const { return BMediaEventLooper::ControlPort(); }
	virtual void Preroll() {}
	virtual BMediaAddOn* AddOn(int32*) const { return NULL; }

	virtual status_t GetNextInput(int32* cookie, media_input* out)
	{
		if (*cookie != 0)
			return B_ENTRY_NOT_FOUND;
		out->node = Node();
		out->destination.port = ControlPort();
		out->destination.id = 0;
		out->source = media_source::null;
		strcpy(out->name, "Guitar In");
		out->format.type = B_MEDIA_RAW_AUDIO;
		out->format.u.raw_audio = media_raw_audio_format::wildcard;
		*cookie = 1;
		return B_OK;
	}

	virtual status_t AcceptFormat(const media_destination&, media_format* format)
	{
		return format->type == B_MEDIA_RAW_AUDIO ? B_OK : B_MEDIA_BAD_FORMAT;
	}

	virtual status_t Connected(const media_source& source, const media_destination& dest,
		const media_format& format, media_input* out)
	{
		out->node = Node();
		out->source = source;
		out->destination = dest;
		out->format = format;
		fFormat = format.u.raw_audio.format;
		fChannels = format.u.raw_audio.channel_count > 0 ? format.u.raw_audio.channel_count : 1;
		return B_OK;
	}

	virtual void Disconnected(const media_source&, const media_destination&) {}
	virtual status_t FormatChanged(const media_source&, const media_destination&, int32, const media_format&)
	{
		return B_OK;
	}
	virtual void ProducerDataStatus(const media_destination&, int32, bigtime_t) {}
	virtual void DisposeInputCookie(int32) {}
	virtual status_t GetNodeAttributes(media_node_attribute*, size_t, size_t* out_count)
	{
		if (out_count)
			*out_count = 0;
		return B_OK;
	}

	// takes the chosen channel of the hardware buffer as mono float
	virtual void BufferReceived(BBuffer* buffer)
	{
		if (buffer == NULL)
			return;
		const uint8* raw = (const uint8*)buffer->Data();
		size_t bytes = buffer->SizeUsed();
		uint32 ch = fChannels;
		uint32 pick = (uint32)fChannel < ch ? (uint32)fChannel : 0;
		float tmp[kMaxChunk];

		size_t sample = fFormat == media_raw_audio_format::B_AUDIO_FLOAT
			|| fFormat == media_raw_audio_format::B_AUDIO_INT ? 4
			: fFormat == media_raw_audio_format::B_AUDIO_SHORT ? 2 : 1;
		size_t frames = bytes / (ch * sample);
		size_t done = 0;
		while (done < frames) {
			size_t n = frames - done < (size_t)kMaxChunk ? frames - done : (size_t)kMaxChunk;
			for (size_t i = 0; i < n; i++)
				tmp[i] = ReadSample(raw, fFormat, (done + i) * ch + pick);
			for (uint32 c = 0; c < ch && c < (uint32)kPeakChannels; c++) {
				float p = fRing->peak[c].load(std::memory_order_relaxed);
				for (size_t i = 0; i < n; i++) {
					float v = fabsf(ReadSample(raw, fFormat, (done + i) * ch + c));
					if (v > p)
						p = v;
				}
				fRing->peak[c].store(p, std::memory_order_relaxed);
			}
			fRing->Write(tmp, (uint32_t)n);
			done += n;
		}
		buffer->Recycle();
	}

private:
	Ring*	fRing;
	int32	fChannel;
	uint32	fFormat;
	uint32	fChannels;
};


// ------------------------------------------------------------------ settings detection (as in rakarrack / hrecord)

struct DriverProfile {
	const char* devfsSegment;
	const char* settingsFile;
	const char* framesKey;
};

static const DriverProfile kProfiles[] = {
	{ "hda", "hda.settings", "play_buffer_frames" },
	{ "auich", "auich.settings", "buffer_frames" },
	{ "es1370", "es1370.settings", "buffer_frames" },
	{ "echo", "echo.settings", "buffer_frames" },
	{ "emuxki", "emuxki.settings", "buffer_frames" },
	{ "ice1712", "ice1712.settings", "buffer_size" },
};


static std::string
ReadFile(const std::string& path)
{
	std::string out;
	FILE* f = fopen(path.c_str(), "r");
	if (f == NULL)
		return out;
	char b[4096];
	size_t n;
	while ((n = fread(b, 1, sizeof(b), f)) > 0)
		out.append(b, n);
	fclose(f);
	return out;
}


// only an active ('#'-less) line counts; a commented one may be the driver's own example
static bool
FindActiveValue(const std::string& text, const char* key, int32* value)
{
	std::string k(key);
	size_t start = 0;
	while (start < text.size()) {
		size_t end = text.find('\n', start);
		if (end == std::string::npos)
			end = text.size();
		size_t p = start;
		while (p < end && (text[p] == ' ' || text[p] == '\t'))
			p++;
		if (p < end && text[p] != '#' && end - p >= k.size() && text.compare(p, k.size(), k) == 0) {
			size_t after = p + k.size();
			if (after >= end || text[after] == ' ' || text[after] == '\t') {
				*value = atol(text.c_str() + after);
				return true;
			}
		}
		start = end + 1;
	}
	return false;
}


static bool
DetectDriverFrames(int32* frames)
{
	DIR* dir = opendir("/dev/audio/hmulti");
	if (dir == NULL)
		return false;
	const DriverProfile* profile = NULL;
	struct dirent* e;
	while (profile == NULL && (e = readdir(dir)) != NULL) {
		for (size_t i = 0; i < sizeof(kProfiles) / sizeof(kProfiles[0]); i++) {
			if (strcmp(e->d_name, kProfiles[i].devfsSegment) == 0) {
				profile = &kProfiles[i];
				break;
			}
		}
	}
	closedir(dir);
	if (profile == NULL)
		return false;

	BPath path;
	std::string dirName = "/boot/home/config/settings/kernel/drivers";
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("kernel/drivers");
		dirName = path.Path();
	}
	std::string text = ReadFile(dirName + "/" + profile->settingsFile);
	if (text.empty())
		return false;
	return FindActiveValue(text, profile->framesKey, frames) && *frames > 0;
}


static bool
DetectOutputRate(uint32* rate)
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL)
		return false;
	media_node node;
	if (roster->GetAudioOutput(&node) != B_OK)
		return false;
	media_output outs[8];
	int32 count = 0;
	bool found = false;
	if (roster->GetAllOutputsFor(node, outs, 8, &count) == B_OK) {
		for (int32 i = 0; i < count && !found; i++) {
			media_format f;
			if (roster->GetFormatFor(outs[i], &f) == B_OK && f.type == B_MEDIA_RAW_AUDIO
					&& f.u.raw_audio.frame_rate > 0) {
				*rate = (uint32)f.u.raw_audio.frame_rate;
				found = true;
			}
		}
	}
	roster->ReleaseNode(node);
	return found;
}


// the non-real-time fallback keeps the buffer's duration when the rate differs from 48 kHz
static int32
ScaleFrames(uint32 rate)
{
	static const int32 common[] = { 256, 512, 1024, 2048, 4096 };
	double target = kDefaultFrames48k * (double)rate / 48000.0;
	int32 best = common[0];
	for (size_t i = 1; i < sizeof(common) / sizeof(common[0]); i++) {
		if (fabs(target - common[i]) < fabs(target - best))
			best = common[i];
	}
	return best;
}


// ------------------------------------------------------------------ GxHaikuAudio

GxHaikuAudio::GxHaikuAudio()
	:
	fProcess(NULL),
	fProcessArg(NULL),
	fPlayer(NULL),
	fInput(NULL),
	fRing(NULL),
	fRate(48000),
	fFrames(1024),
	fRealtime(false),
	fInputChannel(0),
	fIn(NULL),
	fOutL(NULL),
	fOutR(NULL),
	fPrioritySet(false),
	fCushion(1.0f),
	fPrimed(false),
	fLinked(false),
	fLastCallbackUs(0)
{
	ResetStats();
}


GxHaikuAudio::~GxHaikuAudio()
{
	Stop();
}


bool
GxHaikuAudio::DetectSettings(uint32* rate, int32* frames)
{
	uint32 r = 0;
	*rate = DetectOutputRate(&r) ? r : 48000;
	int32 f = 0;
	if (DetectDriverFrames(&f)) {
		*frames = f;
		return true;
	}
	*frames = ScaleFrames(*rate);
	return false;
}


status_t
GxHaikuAudio::Start(GxHaikuProcessFn fn, void* arg, uint32 rate, int32 frames, int32 inputChannel)
{
	if (fPlayer != NULL)
		return B_BUSY;
	fError.clear();
	fProcess = fn;
	fProcessArg = arg;
	fInputChannel = inputChannel;

	uint32 detRate;
	int32 detFrames;
	fRealtime = DetectSettings(&detRate, &detFrames);
	fRate = rate ? rate : detRate;
	fFrames = frames ? frames : detFrames;
	if (fFrames > kMaxChunk)
		fFrames = kMaxChunk;

	fRing = new Ring(fRate);	// about one second; far more than the few buffers we actually use
	fIn = new float[kMaxChunk];
	fOutL = new float[kMaxChunk];
	fOutR = new float[kMaxChunk];
	ResetStats();
	fPrioritySet = false;
	fPrimed = false;

	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL) {
		fError = "no Media Server (is a BApplication running?)";
		Stop();
		return B_ERROR;
	}

	// input: failing to get it is not fatal; the engine then runs on silence and says so
	fInput = new InputNode(fRing, fInputChannel);
	status_t err = roster->RegisterNode(fInput);
	if (err == B_OK) {
		err = _ConnectInput();
		if (err != B_OK) {
			fError = std::string("audio input not connected: ") + strerror(err);
			_Disconnect();
			roster->UnregisterNode(fInput);
			fInput->Release();
			fInput = NULL;
		}
	} else {
		fError = std::string("cannot register the input node: ") + strerror(err);
		fInput->Release();
		fInput = NULL;
	}

	// output
	media_raw_audio_format format;
	memset(&format, 0, sizeof(format));
	format.format = media_raw_audio_format::B_AUDIO_FLOAT;
	format.channel_count = 2;
	format.frame_rate = (float)fRate;
	format.byte_order = B_MEDIA_HOST_ENDIAN;
	format.buffer_size = fFrames * sizeof(float) * 2;

	fPlayer = new BSoundPlayer(&format, "Guitarix-Out", _PlayCallback, NULL, this);
	if (fPlayer->InitCheck() != B_OK) {
		fError = "cannot open the audio output";
		status_t e = fPlayer->InitCheck();
		delete fPlayer;
		fPlayer = NULL;
		Stop();
		return e;
	}
	fPlayer->Start();
	fPlayer->SetHasData(true);
	return B_OK;
}


status_t
GxHaikuAudio::_ConnectInput()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	media_node hardware;
	status_t err = roster->GetAudioInput(&hardware);
	if (err != B_OK)
		return err;

	media_output hwOut;
	int32 count = 0;
	err = roster->GetFreeOutputsFor(hardware, &hwOut, 1, &count, B_MEDIA_RAW_AUDIO);
	if (err != B_OK || count == 0) {
		media_output outs[16];
		int32 n = 0;
		count = 0;
		if (roster->GetAllOutputsFor(hardware, outs, 16, &n) == B_OK) {
			for (int32 i = 0; i < n; i++) {
				if (outs[i].destination.port == 0) {
					hwOut = outs[i];
					count = 1;
					break;
				}
			}
		}
		if (count == 0)
			return B_BUSY;	// all pins busy: the Media Server may need a restart
	}

	media_input myIn;
	err = roster->GetFreeInputsFor(fInput->Node(), &myIn, 1, &count, B_MEDIA_RAW_AUDIO);
	if (err != B_OK)
		return err;

	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	err = roster->Connect(hwOut.source, myIn.destination, &format, &hwOut, &myIn);
	if (err != B_OK)
		return err;
	fHardware = hwOut.node;
	fLinkSource = hwOut.source;
	fLinkDest = myIn.destination;
	fLinked = true;

	// the same time source for both ends, then start them "now"
	BTimeSource* ts = roster->MakeTimeSourceFor(fInput->Node());
	if (ts != NULL) {
		roster->SetTimeSourceFor(fInput->Node().node, ts->Node().node);
		if (!ts->IsRunning()) {
			roster->StartTimeSource(ts->Node(), system_time());
			snooze(5000);
		}
		roster->StartNode(fInput->Node(), 0);
		if (hardware.node != ts->Node().node)
			roster->StartNode(hardware, 0);
		ts->Release();
	}
	return B_OK;
}


void
GxHaikuAudio::_Disconnect()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (roster == NULL || fInput == NULL || !fLinked)
		return;
	roster->Disconnect(fHardware.node, fLinkSource, fInput->Node().node, fLinkDest);
	fLinked = false;
}


void
GxHaikuAudio::Stop()
{
	BMediaRoster* roster = BMediaRoster::Roster();
	if (fPlayer != NULL) {
		fPlayer->Stop(true, true);
		delete fPlayer;
		fPlayer = NULL;
	}
	if (fInput != NULL && roster != NULL) {
		_Disconnect();
		roster->StopNode(fInput->Node(), 0, true);
		roster->UnregisterNode(fInput);
		fInput->Release();
		fInput = NULL;
	}
	delete fRing;
	fRing = NULL;
	delete[] fIn;
	delete[] fOutL;
	delete[] fOutR;
	fIn = fOutL = fOutR = NULL;
}


void
GxHaikuAudio::_PlayCallback(void* cookie, void* buffer, size_t size, const media_raw_audio_format& format)
{
	static_cast<GxHaikuAudio*>(cookie)->_Play(buffer, size, format);
}


void
GxHaikuAudio::_Play(void* buffer, size_t size, const media_raw_audio_format& format)
{
	if (fRing == NULL || fProcess == NULL) {
		memset(buffer, 0, size);
		return;
	}
	if (!fPrioritySet) {
		set_thread_priority(find_thread(NULL), B_REAL_TIME_PRIORITY);
#if defined(__SSE__)
		_mm_setcsr(_mm_getcsr() | 0x8040);		// flush denormals to zero
#endif
		fPrioritySet = true;
	}

	int64 start = system_time();
	if (fLastCallbackUs != 0) {
		double period = (double)(start - fLastCallbackUs);
		fPeriodSum += period;
		if (period > fPeriodMax)
			fPeriodMax = period;
	}
	fLastCallbackUs = start;

	bool isFloat = format.format == media_raw_audio_format::B_AUDIO_FLOAT;
	size_t frames = size / (2 * (isFloat ? sizeof(float) : sizeof(int16)));
	bool haveInput = fInput != NULL;

	size_t done = 0;
	while (done < frames) {
		uint32 n = (uint32)(frames - done < (size_t)kMaxChunk ? frames - done : (size_t)kMaxChunk);

		if (haveInput) {
			uint32 avail = fRing->Available();
			// the two hardware clocks run apart in phase: wait until a cushion of one extra buffer has built up
			// before reading, and again after an underrun
			uint32 cushion = (uint32)(n * fCushion);
			if (!fPrimed && avail >= n + cushion) {
				fRing->KeepNewest(n + cushion);		// start at the cushion, not at whatever piled up
				avail = fRing->Available();
				fPrimed = true;
			}
			// the input side ran ahead (this callback stalled): stay close to live
			if (avail > n * 4 + cushion) {
				fRing->KeepNewest(n + cushion);
				fCatchUps++;
				avail = fRing->Available();
			}
			int32 fill = (int32)avail;
			if (fill < fFillMin)
				fFillMin = fill;
			if (fill > fFillMax)
				fFillMax = fill;
			fFillSum += fill;
			if (fPrimed && avail >= n) {
				fRing->Read(fIn, n);
			} else {
				memset(fIn, 0, n * sizeof(float));
				if (fPrimed) {
					fUnderruns++;
					fPrimed = false;
				}
			}
		} else {
			memset(fIn, 0, n * sizeof(float));
		}

		for (uint32 i = 0; i < n; i++) {
			if (!(fIn[i] == fIn[i]))			// NaN guard
				fIn[i] = 0.0f;
		}

		fProcess(n, fIn, fOutL, fOutR, fProcessArg);

		if (isFloat) {
			float* out = (float*)buffer + done * 2;
			for (uint32 i = 0; i < n; i++) {
				out[i * 2] = fOutL[i];
				out[i * 2 + 1] = fOutR[i];
			}
		} else {
			int16* out = (int16*)buffer + done * 2;
			for (uint32 i = 0; i < n; i++) {
				float l = fOutL[i] > 1.0f ? 1.0f : fOutL[i] < -1.0f ? -1.0f : fOutL[i];
				float r = fOutR[i] > 1.0f ? 1.0f : fOutR[i] < -1.0f ? -1.0f : fOutR[i];
				out[i * 2] = (int16)(l * 32767.0f);
				out[i * 2 + 1] = (int16)(r * 32767.0f);
			}
		}
		done += n;
	}

	fCallbacks++;
	double used = (double)(system_time() - start);
	double budget = (double)frames * 1000000.0 / (double)fRate;
	fLoad = fLoad * 0.9 + (used / budget * 100.0) * 0.1;
}


void
GxHaikuAudio::ResetStats()
{
	fCallbacks = fUnderruns = fCatchUps = 0;
	fFillMin = 0x7fffffff;
	fFillMax = 0;
	fPeriodSum = fPeriodMax = fLoad = fFillSum = 0;
	fLastCallbackUs = 0;
	if (fRing != NULL)
		fRing->overrunFrames = 0;
}


void
GxHaikuAudio::GetStats(GxHaikuAudioStats* s) const
{
	s->sampleRate = fRate;
	s->bufferFrames = fFrames;
	s->realtimeConfigured = fRealtime;
	s->inputConnected = fInput != NULL;
	s->callbacks = fCallbacks;
	s->underruns = fUnderruns;
	s->overruns = fRing != NULL ? fRing->overrunFrames.load() : 0;
	s->catchUps = fCatchUps;
	s->inputFillMin = fFillMin == 0x7fffffff ? 0 : fFillMin;
	s->inputFillMax = fFillMax;
	s->periodAvgUs = fCallbacks > 1 ? fPeriodSum / (double)(fCallbacks - 1) : 0;
	s->periodMaxUs = fPeriodMax;
	s->loadPercent = fLoad;
	double avgFill = fCallbacks ? fFillSum / (double)fCallbacks : 0;
	for (int i = 0; i < 2; i++)
		s->inputPeak[i] = fRing != NULL ? fRing->peak[i].exchange(0.0f) : 0.0f;
	s->estimatedLatencyMs = (avgFill + fFrames) * 1000.0 / (double)(fRate ? fRate : 1);
}
