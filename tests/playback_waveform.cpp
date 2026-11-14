#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "soloud.h"
#include "soloud_wav.h"
#include "soloud_wavstream.h"

static int failures = 0;
static int checks = 0;

static void checkNear(const char *aWhat, float aActual, float aExpected, float aTolerance = 0.00001f)
{
	checks++;
	if (fabs(aActual - aExpected) > aTolerance)
	{
		printf("FAIL %s: got %.8f expected %.8f\n", aWhat, aActual, aExpected);
		failures++;
	}
}

static void checkTrue(const char *aWhat, bool aValue)
{
	checks++;
	if (!aValue)
	{
		printf("FAIL %s\n", aWhat);
		failures++;
	}
}

static void write32(unsigned char *aDst, uint32_t aValue)
{
	aDst[0] = (unsigned char)aValue;
	aDst[1] = (unsigned char)(aValue >> 8);
	aDst[2] = (unsigned char)(aValue >> 16);
	aDst[3] = (unsigned char)(aValue >> 24);
}

static void write16(unsigned char *aDst, uint16_t aValue)
{
	aDst[0] = (unsigned char)aValue;
	aDst[1] = (unsigned char)(aValue >> 8);
}

static unsigned int makeWav(unsigned char *aDst)
{
	memcpy(aDst, "RIFF", 4);
	write32(aDst + 4, 36);
	memcpy(aDst + 8, "WAVEfmt ", 8);
	write32(aDst + 16, 16);
	write16(aDst + 20, 1);
	write16(aDst + 22, 1);
	write32(aDst + 24, 1000);
	write32(aDst + 28, 2000);
	write16(aDst + 32, 2);
	write16(aDst + 34, 16);
	memcpy(aDst + 36, "data", 4);
	write32(aDst + 40, 16);

	uint16_t samples[8] = { 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000 };
	unsigned int i;
	for (i = 0; i < 8; i++)
		write16(aDst + 44 + i * 2, samples[i]);
	return 44 + 16;
}

static void setUnityPan(SoLoud::Soloud &aSoloud, SoLoud::handle aVoice)
{
	aSoloud.setPause(aVoice, true);
	aSoloud.setPanAbsolute(aVoice, 1.0f, 1.0f);
	aSoloud.setVolume(aVoice, 1.0f);
	aSoloud.setPause(aVoice, false);
}

static void testWavLoop()
{
	SoLoud::Soloud soloud;
	checkTrue("init", soloud.init(0, SoLoud::Soloud::NULLDRIVER, 1000, 512, 2) == SoLoud::SO_NO_ERROR);
	soloud.setMainResampler(SoLoud::Soloud::RESAMPLER_POINT);
	soloud.setPostClipScaler(1.0f);

	float data[8] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f };
	SoLoud::Wav wav;
	checkTrue("load raw wav", wav.loadRawWave(data, 8, 1000.0f, 1, true, false) == SoLoud::SO_NO_ERROR);
	wav.setLooping(true);
	checkTrue("set loop range", wav.setLoopRange(0.002, 0.006) == SoLoud::SO_NO_ERROR);
	checkTrue("end-of-stream range accepted", wav.setLoopRange(2.0, 0.0) == SoLoud::SO_NO_ERROR);
	checkTrue("negative start rejected", wav.setLoopRange(-1.0, 0.002) != SoLoud::SO_NO_ERROR);
	checkTrue("empty range rejected", wav.setLoopRange(0.002, 0.002) != SoLoud::SO_NO_ERROR);
	checkTrue("NaN range rejected", wav.setLoopRange(NAN, 0.002) != SoLoud::SO_NO_ERROR);
	checkTrue("end before start rejected", wav.setLoopEnd(0.001) != SoLoud::SO_NO_ERROR);
	checkTrue("restore loop range", wav.setLoopRange(0.002, 0.006) == SoLoud::SO_NO_ERROR);
	double loopStart, loopEnd;
	wav.getLoopRange(loopStart, loopEnd);
	checkNear("loop start getter", (float)loopStart, 0.002f);
	checkNear("loop end getter", (float)loopEnd, 0.006f);

	SoLoud::handle h = soloud.play(wav);
	setUnityPan(soloud, h);
	float out[24] = { 0 };
	soloud.mix(out, 12);

	// Intro 0..5, then loop 2..5 with no gap or repeated boundary sample.
	float expected[12] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f,
		0.3f, 0.4f, 0.5f, 0.6f, 0.3f, 0.4f };
	unsigned int i;
	for (i = 0; i < 12; i++)
	{
		checkNear("loop left", out[i * 2], expected[i]);
		checkNear("loop right", out[i * 2 + 1], expected[i]);
	}
	checkTrue("loop counter increments", soloud.getLoopCount(h) > 0);

	soloud.stopAll();
	wav.setLoopRange(2.0, 0.0); // start past stream length: clamp to whole file
	h = soloud.play(wav);
	setUnityPan(soloud, h);
	soloud.mix(out, 12);
	float fallbackExpected[12] = { 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f,
		0.1f, 0.2f, 0.3f, 0.4f };
	for (i = 0; i < 12; i++)
		checkNear("out-of-range loop fallback", out[i * 2], fallbackExpected[i]);
	soloud.deinit();
}

static void testFadeAndStop()
{
	SoLoud::Soloud soloud;
	checkTrue("init", soloud.init(0, SoLoud::Soloud::NULLDRIVER, 1000, 512, 2) == SoLoud::SO_NO_ERROR);
	soloud.setMainResampler(SoLoud::Soloud::RESAMPLER_POINT);
	soloud.setPostClipScaler(1.0f);

	float data[2048];
	unsigned int i;
	for (i = 0; i < 2048; i++)
		data[i] = 1.0f;
	SoLoud::Wav wav;
	wav.loadRawWave(data, 2048, 1000.0f, 1, true, false);
	wav.setFadeIn(0.004);
	wav.setFadeOut(0.004);

	SoLoud::handle h = soloud.play(wav);
	float out[8] = { 0 };
	const float pan = 0.70710678f;
	soloud.mix(out, 4);
	checkNear("fade-in 1", out[0], pan * 0.25f);
	checkNear("fade-in 2", out[2], pan * 0.50f);
	checkNear("fade-in 3", out[4], pan * 0.75f);
	checkNear("fade-in 4", out[6], pan);

	soloud.mix(out, 4);
	checkNear("fade-in steady", out[0], pan);

	soloud.stopWithFade(h, 0.004);
	soloud.mix(out, 4);
	checkNear("fade-out 1", out[0], pan * 0.75f);
	checkNear("fade-out 2", out[2], pan * 0.50f);
	checkNear("fade-out 3", out[4], pan * 0.25f);
	checkNear("fade-out 4", out[6], 0.0f);

	soloud.mix(out, 4);
	checkNear("silent after fade stop", out[0], 0.0f);
	checkNear("silent after fade stop 2", out[2], 0.0f);

	h = soloud.play(wav);
	soloud.mix(out, 4);
	soloud.mix(out, 4);
	soloud.fadeOutAndStop(h);
	soloud.mix(out, 4);
	checkTrue("default fade-out is nonzero", out[0] != 0.0f);
	soloud.mix(out, 4);
	checkNear("silent after default fade stop", out[0], 0.0f);

	h = soloud.play(wav);
	soloud.mix(out, 4);
	checkTrue("hard stop input nonzero", out[0] != 0.0f);
	soloud.stop(h);
	for (i = 0; i < 8; i++)
		out[i] = 9.0f;
	soloud.mix(out, 4);
	for (i = 0; i < 8; i++)
		checkNear("hard stop silence", out[i], 0.0f);

	wav.setFadeIn(0);
	h = soloud.play(wav);
	soloud.mix(out, 4);
	soloud.fadeIn(h, 0.004);
	soloud.mix(out, 4);
	checkNear("live fade-in 1", out[0], pan * 0.25f);
	checkNear("live fade-in 2", out[2], pan * 0.50f);
	checkNear("live fade-in 3", out[4], pan * 0.75f);
	checkNear("live fade-in 4", out[6], pan);

	soloud.deinit();
}

static void testHardStopLatency()
{
	SoLoud::Soloud soloud;
	checkTrue("init 48k", soloud.init(0, SoLoud::Soloud::NULLDRIVER, 48000, 512, 2) == SoLoud::SO_NO_ERROR);
	soloud.setMainResampler(SoLoud::Soloud::RESAMPLER_POINT);
	soloud.setPostClipScaler(1.0f);

	float data[48000];
	unsigned int i;
	for (i = 0; i < 48000; i++)
		data[i] = 1.0f;
	SoLoud::Wav wav;
	wav.loadRawWave(data, 48000, 48000.0f, 1, true, false);

	SoLoud::handle h = soloud.play(wav);
	float out[96] = { 0 };
	soloud.mix(out, 48); // 1 ms of audio at 48 kHz
	checkTrue("stop latency input nonzero", out[0] != 0.0f);
	soloud.stop(h);
	out[0] = 9.0f;
	soloud.mix(out, 1); // 1/48000 s later
	checkNear("stop is silent on next sample", out[0], 0.0f);
	soloud.deinit();
}

static void testWavStreamLoop()
{
	SoLoud::Soloud soloud;
	checkTrue("init", soloud.init(0, SoLoud::Soloud::NULLDRIVER, 1000, 512, 2) == SoLoud::SO_NO_ERROR);
	soloud.setMainResampler(SoLoud::Soloud::RESAMPLER_POINT);
	soloud.setPostClipScaler(1.0f);

	unsigned char wavData[60];
	unsigned int wavLength = makeWav(wavData);
	SoLoud::WavStream wav;
	checkTrue("load stream wav", wav.loadMem(wavData, wavLength, true, false) == SoLoud::SO_NO_ERROR);
	wav.setLooping(true);
	checkTrue("set stream loop range", wav.setLoopRange(0.002, 0.006) == SoLoud::SO_NO_ERROR);

	SoLoud::handle h = soloud.play(wav);
	setUnityPan(soloud, h);
	float out[24] = { 0 };
	soloud.mix(out, 12);

	float scale = 1.0f / 32768.0f;
	float expected[12] = { 1000 * scale, 2000 * scale, 3000 * scale, 4000 * scale,
		5000 * scale, 6000 * scale, 3000 * scale, 4000 * scale,
		5000 * scale, 6000 * scale, 3000 * scale, 4000 * scale };
	unsigned int i;
	for (i = 0; i < 12; i++)
	{
		checkNear("stream loop left", out[i * 2], expected[i]);
		checkNear("stream loop right", out[i * 2 + 1], expected[i]);
	}
	soloud.deinit();
}

int main()
{
	testWavLoop();
	testFadeAndStop();
	testWavStreamLoop();
	testHardStopLatency();

	printf("%d checks, %d failure(s)\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
