// SoLoud mixing-path benchmark: times Soloud::mix at a given voice count
// and dumps the mixed output for bit-exact comparison between builds.
//
// Usage: mixbench <voices> <buffers> <outfile.raw> [resampler] [channels]
#include "soloud.h"
#include "soloud_wav.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <vector>

static unsigned int g_rng = 22222u;
static float frand()
{
	g_rng = g_rng * 1664525u + 1013904223u;
	return ((g_rng >> 8) & 0xFFFFFF) * (1.0f / 8388608.0f) - 1.0f;
}

static unsigned long long fnv1a(const unsigned char *aData, size_t aLen, unsigned long long aHash)
{
	for (size_t i = 0; i < aLen; i++)
	{
		aHash ^= aData[i];
		aHash *= 1099511628211ULL;
	}
	return aHash;
}

int main(int argc, char **argv)
{
	if (argc < 4)
	{
		printf("usage: mixbench <voices> <buffers> <outfile.raw>\n");
		return 1;
	}
	int voices = atoi(argv[1]);
	int buffers = atoi(argv[2]);
	const char *outfile = argv[3];
	int resampler = argc > 4 ? atoi(argv[4]) : 1; // default RESAMPLER_LINEAR
	int channels = argc > 5 ? atoi(argv[5]) : 2;
	int noloop = argc > 6 ? atoi(argv[6]) : 0;

	const unsigned int samplerate = 44100;
	const unsigned int bufSamples = 2048;

	SoLoud::Soloud soloud;
	if (soloud.init(0, SoLoud::Soloud::NULLDRIVER, samplerate, 2048, channels) != 0)
	{
		printf("init failed\n");
		return 1;
	}
	soloud.setMaxActiveVoiceCount(voices < 255 ? voices : 255);
	soloud.setMainResampler(resampler);

	// Deterministic source material: mono 22.05kHz (upsample x2) and
	// stereo 48kHz (downsample), so the resampler is always exercised.
	const int monoLen = 22050 * 2;
	std::vector<float> monoData(monoLen);
	for (int i = 0; i < monoLen; i++)
	{
		float t = i / 22050.0f;
		monoData[i] = 0.4f * sinf(t * 440.0f * 6.2831853f)
		            + 0.2f * sinf(t * 1330.0f * 6.2831853f)
		            + 0.1f * frand();
	}
	const int stereoLen = 48000 * 2;
	std::vector<float> stereoData(stereoLen * 2);
	for (int i = 0; i < stereoLen; i++)
	{
		float t = i / 48000.0f;
		stereoData[i * 2 + 0] = 0.35f * sinf(t * 220.0f * 6.2831853f) + 0.15f * frand();
		stereoData[i * 2 + 1] = 0.35f * sinf(t * 330.0f * 6.2831853f) + 0.15f * frand();
	}

	SoLoud::Wav monoWav, stereoWav;
	monoWav.loadRawWave(&monoData[0], monoLen, 22050.0f, 1, true);
	stereoWav.loadRawWave(&stereoData[0], stereoLen * 2, 48000.0f, 2, true);
	monoWav.setLooping(!noloop);
	stereoWav.setLooping(!noloop);

	std::vector<SoLoud::handle> handles(voices);
	for (int i = 0; i < voices; i++)
	{
		SoLoud::Wav &w = (i & 1) ? stereoWav : monoWav;
		float vol = 0.4f + 0.1f * (i % 5);
		float pan = 0.25f * ((i % 7) - 3);
		handles[i] = soloud.play(w, vol, pan);
		// ~10% of voices keep a volume fader running so the
		// per-sample gain-ramp path is exercised too.
		if (i % 10 == 3)
			soloud.fadeVolume(handles[i], 0.2f + 0.1f * (i % 3), 2.0);
	}

	std::vector<float> out(bufSamples * channels);
	FILE *f = fopen(outfile, "wb");
	if (!f)
	{
		printf("cannot open %s\n", outfile);
		return 1;
	}

	// Warm-up (voice activation, resample buffer mapping)
	for (int i = 0; i < 16; i++)
		soloud.mix(&out[0], bufSamples);

	unsigned long long hash = 1469598103934665603ULL;
	double bestMs = 1e30;
	for (int rep = 0; rep < 3; rep++)
	{
		auto t0 = std::chrono::steady_clock::now();
		for (int b = 0; b < buffers; b++)
		{
			soloud.mix(&out[0], bufSamples);
			if (rep == 0)
			{
				hash = fnv1a((const unsigned char *)&out[0], out.size() * sizeof(float), hash);
				fwrite(&out[0], sizeof(float), out.size(), f);
			}
		}
		auto t1 = std::chrono::steady_clock::now();
		double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
		if (ms < bestMs) bestMs = ms;
	}
	fclose(f);

	double audioMs = 1000.0 * buffers * bufSamples / samplerate;
	printf("voices=%d buffers=%d total=%.2fms per_buffer=%.2fus realtime_x=%.2f hash=%016llx\n",
		voices, buffers, bestMs, bestMs * 1000.0 / buffers, audioMs / bestMs, hash);
	soloud.deinit();
	return 0;
}
