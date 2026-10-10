/*
 * Command-line check of the Haiku audio backend (no GTK, no Guitarix engine).
 *
 *   haiku_audio_test [--mode pass|tone|click] [--seconds N] [--frames F] [--rate R] [--channel C] [--gain G] [--threshold T] [--cushion X]
 *
 *   pass   input -> both outputs (a guitar through the machine; mind feedback from speakers)
 *   tone   a quiet 440 Hz sine on the outputs: checks the output path and callback timing without any input
 *   click  an impulse every second on the outputs; when it comes back on the input (cable from output to input, or
 *          a microphone near a speaker) the round trip is reported in ms. This is the real latency number.
 */
#include "gx_haiku_audio.h"

#include <Application.h>
#include <OS.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>


enum { kPass, kTone, kClick };

static int gMode = kPass;
static uint32 gRate = 48000;

// click measurement state (output thread only, except the results)
static uint64 gFrameCounter = 0;		// frames processed so far
static int64 gClickFrame = -1;			// frame at which the last click was emitted (-1 = none pending)
static uint64 gNextClick = 0;
static std::atomic<int> gHits(0);
static double gSumMs = 0, gMinMs = 1e9, gMaxMs = 0;
static double gPhase = 0;
static float gGain = 1.0f;
static float gThreshold = 0.15f;


static void
Process(uint32 n, const float* in, float* outL, float* outR, void*)
{
	for (uint32 i = 0; i < n; i++) {
		float l = 0, r = 0;
		switch (gMode) {
			case kPass:
				l = r = in[i] * gGain;
				break;
			case kTone:
				l = r = 0.1f * sinf((float)gPhase);
				gPhase += 2.0 * M_PI * 440.0 / gRate;
				if (gPhase > 2.0 * M_PI)
					gPhase -= 2.0 * M_PI;
				break;
			case kClick:
				if (gFrameCounter + i == gNextClick) {
					l = r = 0.8f;
					gClickFrame = (int64)(gFrameCounter + i);
					gNextClick += gRate;
				}
				// the click comes back as a sample above the threshold, but not within the first few frames
				if (gClickFrame >= 0 && (int64)(gFrameCounter + i) > gClickFrame + 8
						&& fabsf(in[i]) > gThreshold) {
					double ms = ((int64)(gFrameCounter + i) - gClickFrame) * 1000.0 / gRate;
					if (ms < 900) {
						gSumMs += ms;
						if (ms < gMinMs)
							gMinMs = ms;
						if (ms > gMaxMs)
							gMaxMs = ms;
						gHits++;
					}
					gClickFrame = -1;
				}
				break;
		}
		outL[i] = l;
		outR[i] = r;
	}
	gFrameCounter += n;
}


class TestApp : public BApplication {
public:
	TestApp() : BApplication("application/x-vnd.guitarix-haiku-audio-test") {}
};


int
main(int argc, char** argv)
{
	int seconds = 10;
	int frames = 0;
	int rate = 0;
	int channel = 0;
	float cushion = 1.0f;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
			i++;
			gMode = !strcmp(argv[i], "tone") ? kTone : !strcmp(argv[i], "click") ? kClick : kPass;
		} else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
			seconds = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
			frames = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--rate") && i + 1 < argc)
			rate = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--cushion") && i + 1 < argc)
			cushion = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--gain") && i + 1 < argc)
			gGain = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--threshold") && i + 1 < argc)
			gThreshold = (float)atof(argv[++i]);
		else if (!strcmp(argv[i], "--channel") && i + 1 < argc)
			channel = atoi(argv[++i]);
	}

	TestApp app;

	uint32 detRate;
	int32 detFrames;
	bool rt = GxHaikuAudio::DetectSettings(&detRate, &detFrames);
	printf("Detected: %u Hz, %d-frame buffer, real-time audio %s\n", detRate, detFrames,
		rt ? "configured" : "NOT configured (the app should warn the user)");

	GxHaikuAudio audio;
	audio.SetCushion(cushion);
	status_t err = audio.Start(Process, NULL, rate, frames, channel);
	if (err != B_OK) {
		printf("Start failed: %s (%s)\n", strerror(err), audio.Error().c_str());
		return 1;
	}
	GxHaikuAudioStats s;
	audio.GetStats(&s);
	gRate = s.sampleRate;
	if (!audio.Error().empty())
		printf("Note: %s\n", audio.Error().c_str());
	printf("Running %u Hz, %d frames (%.2f ms per buffer), input %s, mode %s\n", s.sampleRate, s.bufferFrames,
		s.bufferFrames * 1000.0 / s.sampleRate, s.inputConnected ? "connected" : "NOT connected",
		gMode == kPass ? "pass" : gMode == kTone ? "tone" : "click");

	for (int t = 1; t <= seconds; t++) {
		snooze(1000000);
		audio.GetStats(&s);
		printf("%2ds  callbacks %llu  period avg %.0f us max %.0f us  load %.1f%%  input fill %d..%d  "
			"underruns %llu  overrun frames %llu  catch-ups %llu  own latency ~%.1f ms  in peak L %.1f%% R %.1f%%\n",
			t, (unsigned long long)s.callbacks, s.periodAvgUs, s.periodMaxUs, s.loadPercent, s.inputFillMin,
			s.inputFillMax, (unsigned long long)s.underruns, (unsigned long long)s.overruns,
			(unsigned long long)s.catchUps, s.estimatedLatencyMs, s.inputPeak[0] * 100, s.inputPeak[1] * 100);
	}
	if (gMode == kClick) {
		int hits = gHits.load();
		if (hits > 0)
			printf("Round trip: %d clicks heard, avg %.2f ms, min %.2f, max %.2f\n", hits, gSumMs / hits, gMinMs,
				gMaxMs);
		else
			printf("Round trip: no click came back on the input (connect an output to the input first)\n");
	}
	audio.Stop();
	return 0;
}
