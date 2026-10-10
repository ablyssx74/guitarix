/*
 * Haiku audio backend for Guitarix: the Media Kit stands in for JACK.
 *
 * Input:  a BBufferConsumer node connected to the system's audio input (hardware), converted to mono float and put in a
 *         lock-free ring.
 * Output: a BSoundPlayer. Its callback runs the DSP (mono in -> stereo out) on the real-time output thread, reading the
 *         input ring, so there are no locks on the audio path.
 *
 * This file knows nothing about Guitarix; the engine glue (GxJack for Haiku) sets a process callback and starts it.
 */
#ifndef GX_HAIKU_AUDIO_H
#define GX_HAIKU_AUDIO_H

#include <MediaDefs.h>
#include <MediaNode.h>
#include <SupportDefs.h>
#include <stdint.h>
#include <string>


// Called on the real-time output thread. in: nframes mono floats, outL/outR: nframes floats to fill.
typedef void (*GxHaikuProcessFn)(uint32 nframes, const float* in, float* outL, float* outR, void* arg);


struct GxHaikuAudioStats {
	uint32		sampleRate;
	int32		bufferFrames;		// frames per output callback
	bool		realtimeConfigured;	// the sound driver has a small play buffer set (Haiku "real-time audio")
	bool		inputConnected;
	uint64		callbacks;
	uint64		underruns;			// callbacks where the input had fewer frames than needed (silence used)
	uint64		overruns;			// frames the input ring had to drop because it was full
	uint64		catchUps;			// times the input backlog was dropped to stay near live
	int32		inputFillMin;		// frames waiting in the input ring at the start of a callback
	int32		inputFillMax;
	double		periodAvgUs;		// time between output callbacks
	double		periodMaxUs;
	double		loadPercent;		// DSP time as a share of the buffer time (smoothed)
	float		inputPeak[2];		// loudest sample on input channels 1 and 2 since the last call (0..1)
	double		estimatedLatencyMs;	// our own buffering only (input ring + one output buffer); the driver adds more
};


class GxHaikuAudio {
public:
							GxHaikuAudio();
							~GxHaikuAudio();

	// Reads the driver settings: sample rate of the system output and the sound driver's play buffer. Needs a
	// running BApplication. Returns true when the driver has a real-time (small) buffer configured.
	static	bool			DetectSettings(uint32* rate, int32* frames);

	// rate/frames 0 = use what DetectSettings found. inputChannel is the channel of a multi-channel input used as the
	// guitar (0 = left). Returns B_OK, or an error with Error() describing it.
			status_t		Start(GxHaikuProcessFn fn, void* arg, uint32 rate = 0, int32 frames = 0,
								int32 inputChannel = 0);
			void			Stop();
			bool			IsRunning() const { return fPlayer != NULL; }

			void			GetStats(GxHaikuAudioStats* stats) const;
			void			ResetStats();
	// extra input buffering, in output buffers (default 1.0): more is safer, and adds that much latency
			void			SetCushion(float buffers) { fCushion = buffers; }
			const std::string&	Error() const { return fError; }

	// ---- used by the input node and the output callback
			class InputNode;
			struct Ring;
private:
	static	void			_PlayCallback(void* cookie, void* buffer, size_t size,
								const struct media_raw_audio_format& format);
			void			_Play(void* buffer, size_t size, const media_raw_audio_format& format);
			status_t		_ConnectInput();
			void			_Disconnect();

			GxHaikuProcessFn	fProcess;
			void*			fProcessArg;
			class BSoundPlayer*	fPlayer;
			InputNode*		fInput;
			Ring*			fRing;
			uint32			fRate;
			int32			fFrames;
			bool			fRealtime;
			int32			fInputChannel;
			std::string		fError;
			float*			fIn;
			float*			fOutL;
			float*			fOutR;
			bool			fPrioritySet;
			float			fCushion;
			bool			fPrimed;
			bool			fLinked;		// the hardware input is connected to our node
			media_node		fHardware;
			media_source	fLinkSource;
			media_destination	fLinkDest;
			int64			fLastCallbackUs;
			// statistics (written by the output thread only)
			uint64			fCallbacks, fUnderruns, fCatchUps;
			int32			fFillMin, fFillMax;
			double			fPeriodSum, fPeriodMax, fLoad, fFillSum;
};


#endif	// GX_HAIKU_AUDIO_H
