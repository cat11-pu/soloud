/*
SoLoud stability / robustness test harness.

Covers:
  - illegal init parameters (sample rate, channels, buffer size, flags)
  - out-of-range voice slots encoded in garbage handles
  - repeated deinit and voice-group destruction
  - illegal per-voice sample rates / relative play speeds
  - init/deinit cycles (device reconnect simulation)
  - voice churn and bounded mix input

Build and run:
  ./tests/stability/run_stability_tests.sh
*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "soloud.h"
#include "soloud_noise.h"
#include "soloud_thread.h"

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond, msg) do { \
	gChecks++; \
	if (!(cond)) { gFailures++; printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static unsigned int gRandState = 0x12345678;
static unsigned int frand()
{
	gRandState ^= gRandState << 13;
	gRandState ^= gRandState >> 17;
	gRandState ^= gRandState << 5;
	return gRandState;
}

static SoLoud::handle edgeHandle(unsigned int i, SoLoud::handle aValid)
{
	switch (i % 10)
	{
	case 0: return 0;
	case 1: return 0x00001000;            // voice slot 0
	case 2: return 0x00001800;            // slot 2047, beyond VOICE_COUNT
	case 3: return 0xffffffff;
	case 4: return 0xfffff000 | (i * 7);  // voice-group marker
	case 5: return aValid;
	case 6: return aValid + (1 << 12);    // stale play index
	case 7: return frand();
	case 8: return (frand() & 0xfff) | (frand() & 0xfffff000);
	default: return 0x00000fff;           // slot 4094
	}
}

static void testInvalidInitParams()
{
	printf("== invalid init parameters ==\n");
	unsigned int badRates[] = { 1, 100, 7999, 384001, 0x7fffffff, 0xffffffff };
	unsigned int badChannels[] = { 0, 3, 5, 7, 9, 255 };
	unsigned int badBuffers[] = { 1, 8, 15, (1 << 20) + 1, 0xffffffff };

	unsigned int i;
	for (i = 0; i < sizeof(badRates) / sizeof(*badRates); i++)
	{
		SoLoud::Soloud s;
		CHECK(s.init(0, SoLoud::Soloud::NOSOUND, badRates[i], 2048, 2) == SoLoud::INVALID_PARAMETER,
			"init with illegal samplerate rejected");
	}
	for (i = 0; i < sizeof(badChannels) / sizeof(*badChannels); i++)
	{
		SoLoud::Soloud s;
		CHECK(s.init(0, SoLoud::Soloud::NOSOUND, 44100, 2048, badChannels[i]) == SoLoud::INVALID_PARAMETER,
			"init with illegal channel count rejected");
	}
	for (i = 0; i < sizeof(badBuffers) / sizeof(*badBuffers); i++)
	{
		SoLoud::Soloud s;
		CHECK(s.init(0, SoLoud::Soloud::NOSOUND, 44100, badBuffers[i], 2) == SoLoud::INVALID_PARAMETER,
			"init with illegal buffer size rejected");
	}

	SoLoud::Soloud s;
	CHECK(s.init(0x80000000u, SoLoud::Soloud::NOSOUND, 44100, 2048, 2) == SoLoud::INVALID_PARAMETER,
		"init with unknown flags rejected");
	CHECK(s.init(0, SoLoud::Soloud::NOSOUND, 1, 1, 0) == SoLoud::INVALID_PARAMETER,
		"all-bad init rejected");
	CHECK(s.init(0, SoLoud::Soloud::NOSOUND, 44100, 2048, 2) == SoLoud::SO_NO_ERROR,
		"init works after rejections");
	s.deinit();
}

static void testDoubleDestroy()
{
	printf("== double destroy / repeated deinit ==\n");
	SoLoud::Soloud s;
	s.deinit();
	s.deinit();
	CHECK(s.init(0, SoLoud::Soloud::NOSOUND, 44100, 2048, 2) == SoLoud::SO_NO_ERROR, "init");
	s.deinit();
	s.deinit();
	s.deinit();
	CHECK(s.init(0, SoLoud::Soloud::NOSOUND, 48000, 1024, 2) == SoLoud::SO_NO_ERROR,
		"re-init after deinits");
	s.deinit();

	int i;
	for (i = 0; i < 50; i++)
	{
		SoLoud::Soloud t;
		CHECK(t.init(0, SoLoud::Soloud::NOSOUND, 44100, 512, 2) == SoLoud::SO_NO_ERROR,
			"cycle init");
		t.deinit();
	}

	SoLoud::Soloud v;
	v.init(0, SoLoud::Soloud::NOSOUND, 44100, 2048, 2);
	SoLoud::handle g = v.createVoiceGroup();
	CHECK(g != 0, "voice group created");
	CHECK(v.destroyVoiceGroup(g) == SoLoud::SO_NO_ERROR, "first destroyVoiceGroup ok");
	CHECK(v.destroyVoiceGroup(g) == SoLoud::INVALID_PARAMETER,
		"second destroyVoiceGroup rejected");
	v.deinit();
}

static void testInvalidSamplerate()
{
	printf("== illegal per-voice sample rates ==\n");
	SoLoud::Soloud s;
	s.init(0, SoLoud::Soloud::NOSOUND, 44100, 2048, 2);
	SoLoud::Noise noise;
	SoLoud::handle h = s.play(noise);
	CHECK(h != 0, "voice playing");

	float bad[] = { 0.0f, -1.0f, -44100.0f, 1.0e30f,
		0.0f / 0.0f, 1.0f / 0.0f, -1.0f / 0.0f };
	unsigned int i;
	for (i = 0; i < sizeof(bad) / sizeof(*bad); i++)
		CHECK(s.setSamplerate(h, bad[i]) == SoLoud::INVALID_PARAMETER,
			"illegal setSamplerate rejected");

	CHECK(s.setSamplerate(h, 22050.0f) == SoLoud::SO_NO_ERROR, "legal setSamplerate ok");
	CHECK(s.setRelativePlaySpeed(h, 0.0f) == SoLoud::INVALID_PARAMETER,
		"zero play speed rejected");
	CHECK(s.setRelativePlaySpeed(h, 0.0f / 0.0f) == SoLoud::INVALID_PARAMETER,
		"NaN play speed rejected");
	CHECK(s.setRelativePlaySpeed(h, 1.0f / 0.0f) == SoLoud::INVALID_PARAMETER,
		"inf play speed rejected");
	CHECK(s.setRelativePlaySpeed(h, 2.0f) == SoLoud::SO_NO_ERROR, "legal play speed ok");

	CHECK(s.setSamplerate(0x00001800, 44100.0f) == SoLoud::INVALID_PARAMETER,
		"out-of-range handle rejected by setSamplerate");
	CHECK(s.setRelativePlaySpeed(0x00001800, 1.0f) == SoLoud::INVALID_PARAMETER,
		"out-of-range handle rejected by setRelativePlaySpeed");
	CHECK(s.seek(0x00001800, 0.01) == SoLoud::INVALID_PARAMETER,
		"out-of-range handle rejected by seek");

	SoLoud::Thread::sleep(100);
	CHECK(s.isValidVoiceHandle(h), "voice still valid after rejected rates");
	s.deinit();
}

static void testHandleFuzz(SoLoud::Soloud &s, SoLoud::handle aValid, int aIterations)
{
	int i;
	for (i = 0; i < aIterations; i++)
	{
		SoLoud::handle h = edgeHandle(frand(), aValid);
		float f = (frand() % 2000) / 1000.0f - 1.0f;
		s.isValidVoiceHandle(h);
		s.isVoiceGroup(h);
		s.isVoiceGroupEmpty(h);
		s.setVolume(h, f);
		s.getVolume(h);
		s.setPan(h, f);
		s.setPanAbsolute(h, f, f);
		s.setChannelVolume(h, frand() % 16, f);
		s.setSamplerate(h, 44100.0f);
		s.setRelativePlaySpeed(h, 1.0f + f);
		s.getRelativePlaySpeed(h);
		s.setProtectVoice(h, frand() & 1);
		s.getProtectVoice(h);
		s.setPause(h, frand() & 1);
		s.getPause(h);
		s.getStreamTime(h);
		s.getStreamPosition(h);
		s.seek(h, 0.01);
		s.getLoopCount(h);
		s.getInfo(h, frand() % 64);
		s.setDelaySamples(h, frand() % 4096);
		s.schedulePause(h, 0.001);
		s.scheduleStop(h, 0.001);
		s.set3dSourcePosition(h, f, f, f);
		s.set3dSourceVelocity(h, f, f, f);
		s.set3dSourceParameters(h, f, f, f);
		s.set3dSourceAttenuation(h, frand() % 8, f);
		s.fadeVolume(h, f, 0.001);
		s.fadePan(h, f, 0.001);
		s.fadeRelativePlaySpeed(h, 1.0f, 0.001);
		s.oscillateVolume(h, f, 1.0f, 0.001);
		s.oscillatePan(h, f, 1.0f, 0.001);
		s.stop(h);
	}
}

static void testHandleFuzzing()
{
	printf("== garbage handle fuzzing (with live audio thread) ==\n");
	SoLoud::Soloud s;
	s.init(0, SoLoud::Soloud::NOSOUND, 44100, 2048, 2);
	SoLoud::Noise noise;
	SoLoud::handle valid = s.play(noise);
	SoLoud::handle valid3d = s.play3d(noise, 0, 0, 0);
	CHECK(valid != 0 && valid3d != 0, "voices playing");
	CHECK(s.isValidVoiceHandle(valid), "valid handle recognized");
	CHECK(!s.isValidVoiceHandle(0x00001000), "slot 0 handle rejected");
	CHECK(!s.isValidVoiceHandle(0x00001800), "slot 2047 handle rejected");
	CHECK(!s.isValidVoiceHandle(0x00000fff), "slot 4094 handle rejected");

	testHandleFuzz(s, valid, 20000);
	s.update3dAudio();
	testHandleFuzz(s, valid3d, 20000);

	s.stopAll();
	testHandleFuzz(s, valid, 20000);
	s.deinit();
	CHECK(1, "survived handle fuzzing");
}

static void testMixBounds()
{
	printf("== bounded mix input ==\n");
	SoLoud::Soloud s;
	s.init(0, SoLoud::Soloud::NOSOUND, 44100, 512, 2);
	float untouched[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
	s.mix(untouched, 0);
	CHECK(untouched[0] == 1.0f && untouched[3] == 4.0f, "mix(0) is a no-op");
	s.mix(untouched, 0xffffffffu);
	CHECK(untouched[0] == 1.0f && untouched[3] == 4.0f, "oversized mix is rejected");
	s.mix(NULL, 16);
	short untouched16[4] = { 1, 2, 3, 4 };
	s.mixSigned16(untouched16, 0);
	CHECK(untouched16[0] == 1 && untouched16[3] == 4, "mixSigned16(0) is a no-op");
	s.deinit();
}

static void testReconnect()
{
	printf("== deinit/init reconnect recovery ==\n");
	SoLoud::Noise noise;
	SoLoud::Soloud s;
	CHECK(s.init(0, SoLoud::Soloud::NOSOUND, 44100, 2048, 2) == SoLoud::SO_NO_ERROR,
		"initial device init");
	SoLoud::handle first = s.play(noise);
	CHECK(first != 0, "audio before disconnect");
	SoLoud::Thread::sleep(50);

	s.deinit(); // simulated device loss

	int i;
	for (i = 0; i < 100; i++)
	{
		CHECK(s.init(0, SoLoud::Soloud::NOSOUND, 44100, 1024, 2) == SoLoud::SO_NO_ERROR,
			"reconnect cycle init");
		CHECK(s.play(noise) != 0, "reconnect cycle playback");
		if ((i & 15) == 0)
			SoLoud::Thread::sleep(1);
		s.deinit();
	}

	CHECK(s.init(0, SoLoud::Soloud::NOSOUND, 48000, 2048, 2) == SoLoud::SO_NO_ERROR,
		"reconnect init");
	CHECK(s.getBackendSamplerate() == 48000, "reconnected device configuration active");
	SoLoud::handle second = s.play(noise);
	CHECK(second != 0 && s.isValidVoiceHandle(second), "audio resumes after reconnect");
	SoLoud::Thread::sleep(50);
	s.deinit();
}

static void testVoiceChurn()
{
	printf("== voice churn / slot exhaustion ==\n");
	SoLoud::Soloud s;
	s.init(0, SoLoud::Soloud::NOSOUND, 44100, 2048, 2);
	SoLoud::Noise noise;
	int i;
	for (i = 0; i < 3000; i++)
	{
		SoLoud::handle h = s.play(noise, -1.0f, 0.0f, false);
		if (h != 0)
		{
			s.setVolume(h, 0.01f);
			if ((i & 3) == 0) s.stop(h);
		}
	}
	SoLoud::Thread::sleep(100);
	s.stopAll();
	s.deinit();
	CHECK(1, "survived voice churn");
}

int main()
{
	testInvalidInitParams();
	testDoubleDestroy();
	testInvalidSamplerate();
	testHandleFuzzing();
	testMixBounds();
	testReconnect();
	testVoiceChurn();

	printf("\n%d checks, %d failures\n", gChecks, gFailures);
	if (gFailures == 0)
		printf("STABILITY TEST PASSED\n");
	return gFailures == 0 ? 0 : 1;
}
