// Benchmark harness for SoLoud mixing/resampling hot paths.
// Usage: mixbench <voices> <frames> <iters> [dumpfile]
// Mixes <frames>-sample frames <iters> times, reports min/median per-frame
// time, and optionally dumps the mixed output of the first frames for
// bit-exact (per-sample) comparison between builds.

#include "soloud.h"
#include "soloud_wav.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

// Per-frame CPU time is robust against other load on the machine.
static double cputime_us()
{
	timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec * 1e-3;
}

static void put32(std::vector<unsigned char> &v, unsigned int x)
{
	v.push_back(x & 0xff);
	v.push_back((x >> 8) & 0xff);
	v.push_back((x >> 16) & 0xff);
	v.push_back((x >> 24) & 0xff);
}

static void put16(std::vector<unsigned char> &v, unsigned int x)
{
	v.push_back(x & 0xff);
	v.push_back((x >> 8) & 0xff);
}

static void puttag(std::vector<unsigned char> &v, const char *s)
{
	v.push_back(s[0]);
	v.push_back(s[1]);
	v.push_back(s[2]);
	v.push_back(s[3]);
}

// Build a 44.1kHz 16-bit stereo wav in memory (deterministic contents).
static std::vector<unsigned char> makewav(float seconds, unsigned int seed)
{
	const int rate = 44100;
	const int frames = (int)(seconds * rate);
	std::vector<unsigned char> out;
	std::vector<short> pcm(frames * 2);
	unsigned int s = seed;
	int i;
	for (i = 0; i < frames; i++)
	{
		float t = i / (float)rate;
		float l = 0.30f * sinf(2.0f * 3.14159265f * 220.0f * t) + 0.20f * sinf(2.0f * 3.14159265f * 331.0f * t);
		float r = 0.30f * sinf(2.0f * 3.14159265f * 277.0f * t) + 0.20f * sinf(2.0f * 3.14159265f * 419.0f * t);
		s = s * 1664525u + 1013904223u;
		float n = ((s >> 8) & 0xffff) / 65535.0f - 0.5f;
		l += 0.05f * n;
		r -= 0.05f * n;
		pcm[i * 2 + 0] = (short)(l * 24000.0f);
		pcm[i * 2 + 1] = (short)(r * 24000.0f);
	}
	unsigned int datasize = frames * 2 * 2;
	puttag(out, "RIFF");
	put32(out, 36 + datasize);
	puttag(out, "WAVE");
	puttag(out, "fmt ");
	put32(out, 16);
	put16(out, 1);      // PCM
	put16(out, 2);      // stereo
	put32(out, rate);
	put32(out, rate * 4);
	put16(out, 4);
	put16(out, 16);
	puttag(out, "data");
	put32(out, datasize);
	out.insert(out.end(), (unsigned char *)pcm.data(), (unsigned char *)pcm.data() + datasize);
	return out;
}

// Soloud subclass so the bench can raise the active voice cap pre-init.
class BenchSoloud : public SoLoud::Soloud
{
public:
	void setMaxActive(unsigned int aVoices) { mMaxActiveVoices = aVoices; }
};

int main(int argc, char **argv)
{
	int voices = argc > 1 ? atoi(argv[1]) : 32;
	int frames = argc > 2 ? atoi(argv[2]) : 1024;
	int iters = argc > 3 ? atoi(argv[3]) : 3000;
	const char *dumpfile = argc > 4 ? argv[4] : 0;

	BenchSoloud soloud;
	soloud.setMaxActive(64);
	soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER, 48000, 2048, 2);

	std::vector<unsigned char> wavdata = makewav(4.0f, 12345);
	SoLoud::Wav wav;
	wav.loadMem(wavdata.data(), (unsigned int)wavdata.size(), true, false);
	wav.setLooping(1);

	int i;
	for (i = 0; i < voices; i++)
	{
		int h = soloud.play(wav, 0.7f);
		soloud.setPan(h, -0.8f + 1.6f * (i / (float)(voices > 1 ? voices - 1 : 1)));
		soloud.setRelativePlaySpeed(h, 0.9f + 0.05f * (i % 5));
		soloud.setLooping(h, 1);
	}

	std::vector<float> buf(frames * 2);

	// Warmup (also settles faders/resampler state)
	for (i = 0; i < 200; i++)
		soloud.mix(buf.data(), frames);

	// Dump phase: deterministic output for bit-exact comparison
	if (dumpfile)
	{
		FILE *f = fopen(dumpfile, "wb");
		int d;
		for (d = 0; d < 64; d++)
		{
			soloud.mix(buf.data(), frames);
			fwrite(buf.data(), sizeof(float), frames * 2, f);
		}
		fclose(f);
	}

	// Timing phase
	std::vector<double> times(iters);
	for (i = 0; i < iters; i++)
	{
		double t0 = cputime_us();
		soloud.mix(buf.data(), frames);
		double t1 = cputime_us();
		times[i] = t1 - t0;
	}
	std::sort(times.begin(), times.end());
	double tmin = times[0];
	double tmed = times[iters / 2];
	double tp10 = times[iters / 10];
	printf("voices=%d frames=%d iters=%d  min=%.2fus p10=%.2fus median=%.2fus  (frame budget %.1fus @48kHz)\n",
		voices, frames, iters, tmin, tp10, tmed, frames / 48.0 * 1000.0);
	return 0;
}
