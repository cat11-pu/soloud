#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "soloud.h"
#include "soloud_wav.h"

namespace
{
	int gTests = 0;
	int gFailures = 0;

	void check(bool aCondition, const char *aName)
	{
		gTests++;
		if (!aCondition)
		{
			gFailures++;
			printf("FAIL: %s\n", aName);
		}
	}

	void expectResult(SoLoud::result aActual, SoLoud::result aExpected, const char *aName)
	{
		gTests++;
		if (aActual != aExpected)
		{
			gFailures++;
			printf("FAIL: %s (expected %u, got %u)\n", aName, aExpected, aActual);
		}
	}

	uint64_t hashFloatBuffer(const float *aBuffer, unsigned int aCount)
	{
		uint64_t hash = 1469598103934665603ULL;
		const unsigned char *bytes = (const unsigned char *)aBuffer;
		for (unsigned int i = 0; i < aCount * sizeof(float); i++)
		{
			hash ^= bytes[i];
			hash *= 1099511628211ULL;
		}
		return hash;
	}

	class DeterministicSource : public SoLoud::AudioSource
	{
	public:
		explicit DeterministicSource(unsigned int aSampleCount)
			: mSampleCount(aSampleCount)
		{
			mBaseSamplerate = 44100.0f;
			mChannels = 1;
		}

		float sampleAt(unsigned int aIndex) const
		{
			uint32_t value = aIndex * 1664525u + 1013904223u;
			return (float)((int32_t)(value >> 16) - 32768) / 32768.0f;
		}

		class Instance : public SoLoud::AudioSourceInstance
		{
		public:
			explicit Instance(DeterministicSource *aParent)
				: mParent(aParent), mOffset(0)
			{
			}

			unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead, unsigned int)
			{
				unsigned int left = mParent->mSampleCount - mOffset;
				unsigned int copyCount = aSamplesToRead < left ? aSamplesToRead : left;
				for (unsigned int i = 0; i < copyCount; i++)
					aBuffer[i] = mParent->sampleAt(mOffset + i);
				mOffset += copyCount;
				return copyCount;
			}

			bool hasEnded()
			{
				return mOffset >= mParent->mSampleCount;
			}

			SoLoud::result rewind()
			{
				mOffset = 0;
				return SoLoud::SO_NO_ERROR;
			}

		private:
			DeterministicSource *mParent;
			unsigned int mOffset;
		};

		SoLoud::AudioSourceInstance *createInstance()
		{
			return new Instance(this);
		}

		unsigned int mSampleCount;
	};

	void testZeroLengthInputs()
	{
		SoLoud::Soloud soloud;
		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER, 44100, 2048, 2),
			SoLoud::SO_NO_ERROR, "zero-length: initialize engine");

		DeterministicSource empty(0);
		unsigned int handle = soloud.play(empty);
		check(handle != 0, "zero-length: empty source returns a handle");

		float output[512 * 2];
		for (unsigned int i = 0; i < 512 * 2; i++)
			output[i] = 123.0f;
		soloud.mix(output, 512);
		bool silent = true;
		for (unsigned int i = 0; i < 512 * 2; i++)
			if (output[i] != 0.0f)
				silent = false;
		check(silent, "zero-length: empty source produces silence");

		float untouched[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
		soloud.mix(untouched, 0);
		check(untouched[0] == 1.0f && untouched[1] == 2.0f &&
			untouched[2] == 3.0f && untouched[3] == 4.0f,
			"zero-length: mix(0) is a no-op");
		short untouched16[4] = { 1, 2, 3, 4 };
		soloud.mixSigned16(untouched16, 0);
		check(untouched16[0] == 1 && untouched16[1] == 2 &&
			untouched16[2] == 3 && untouched16[3] == 4,
			"zero-length: mixSigned16(0) is a no-op");

		SoLoud::Wav wav;
		unsigned char dummy = 0;
		expectResult(wav.loadMem(&dummy, 0), SoLoud::INVALID_PARAMETER,
			"zero-length: loadMem rejects empty data");
		expectResult(wav.loadRawWave8(&dummy, 0), SoLoud::INVALID_PARAMETER,
			"zero-length: loadRawWave8 rejects empty data");
		expectResult(wav.loadRawWave16((short *)&dummy, 0), SoLoud::INVALID_PARAMETER,
			"zero-length: loadRawWave16 rejects empty data");
		expectResult(wav.loadRawWave((float *)&dummy, 0), SoLoud::INVALID_PARAMETER,
			"zero-length: loadRawWave rejects empty data");

		soloud.deinit();
	}

	void testHugeSampleRates()
	{
		SoLoud::Soloud soloud;
		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
			0xffffffffu, 2048, 2),
			SoLoud::INVALID_PARAMETER, "huge samplerate: UINT_MAX rejected");
		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
			768001, 2048, 2),
			SoLoud::INVALID_PARAMETER, "huge samplerate: above supported maximum rejected");
		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
			192000, 2048, 2),
			SoLoud::SO_NO_ERROR, "huge samplerate: 192 kHz remains valid");
		check(soloud.getBackendSamplerate() == 192000,
			"huge samplerate: legal rate is preserved");
		soloud.deinit();
	}

	void testInvalidChannels()
	{
		const unsigned int invalid[] = { 0, 3, 5, 7, 9, 0xffffffffu };
		for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
		{
			SoLoud::Soloud soloud;
			char name[80];
			snprintf(name, sizeof(name), "invalid channels: %u rejected", invalid[i]);
			expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
				44100, 2048, invalid[i]),
				SoLoud::INVALID_PARAMETER, name);
		}

		const unsigned int valid[] = { 1, 2, 4, 6, 8 };
		for (unsigned int i = 0; i < sizeof(valid) / sizeof(valid[0]); i++)
		{
			SoLoud::Soloud soloud;
			char name[80];
			snprintf(name, sizeof(name), "valid channels: %u accepted", valid[i]);
			expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
				44100, 2048, valid[i]),
				SoLoud::SO_NO_ERROR, name);
			check(soloud.getBackendChannels() == valid[i],
				"valid channels: channel count is preserved");
			soloud.deinit();
		}

		SoLoud::Wav wav;
		float sample = 0.0f;
		expectResult(wav.loadRawWave(&sample, 1, 44100.0f, 0),
			SoLoud::INVALID_PARAMETER, "invalid channels: raw wave zero channels rejected");
		expectResult(wav.loadRawWave(&sample, 1, 44100.0f, 9),
			SoLoud::INVALID_PARAMETER, "invalid channels: raw wave too many channels rejected");
		expectResult(wav.loadRawWave(&sample, 1, 44100.0f, 2),
			SoLoud::INVALID_PARAMETER, "invalid channels: incomplete interleaved frame rejected");
		expectResult(wav.loadRawWave(&sample, 1, 1.0e30f, 1),
			SoLoud::INVALID_PARAMETER, "huge samplerate: raw wave rate rejected");
	}

	void testOversizedBuffers()
	{
		SoLoud::Soloud soloud;
		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
			44100, 0xffffffffu, 2),
			SoLoud::INVALID_PARAMETER, "oversized buffer: UINT_MAX rejected");
		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
			44100, 0x10000001u, 2),
			SoLoud::INVALID_PARAMETER, "oversized buffer: arithmetic wrap input rejected");
	}

	void testRepeatedInitialization()
	{
		SoLoud::Soloud soloud;
		bool allSucceeded = true;
		for (unsigned int i = 0; i < 200; i++)
		{
			unsigned int rate = (i & 1) ? 48000 : 44100;
			unsigned int channels = (i & 2) ? 1 : 2;
			if (soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
				rate, 2048, channels) != SoLoud::SO_NO_ERROR)
				allSucceeded = false;
		}
		check(allSucceeded, "repeated init: 200 consecutive initializations succeed");
		check(soloud.getBackendSamplerate() == 48000,
			"repeated init: last legal configuration is visible");
		soloud.deinit();

		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
			44100, 2048, 2), SoLoud::SO_NO_ERROR, "repeated init: initialize before invalid call");
		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
			0xffffffffu, 2048, 2), SoLoud::INVALID_PARAMETER,
			"repeated init: invalid call reports an error");
		check(soloud.getBackendSamplerate() == 44100,
			"repeated init: invalid call leaves the legal engine intact");
		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
			48000, 2048, 2), SoLoud::SO_NO_ERROR,
			"repeated init: legal call recovers after invalid call");
		soloud.deinit();
	}

	void testErrorStrings()
	{
		SoLoud::Soloud soloud;
		const char *message = soloud.getErrorString(SoLoud::INVALID_PARAMETER);
		check(message != 0, "errors: INVALID_PARAMETER has a message");
		check(strcmp(message, "Some parameter is invalid") == 0,
			"errors: INVALID_PARAMETER message is explicit");
	}

	uint64_t testLegalOutputBaseline()
	{
		SoLoud::Soloud soloud;
		expectResult(soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
			44100, 2048, 2), SoLoud::SO_NO_ERROR, "legal baseline: initialize engine");

		DeterministicSource source(4096);
		unsigned int handle = soloud.play(source, 0.75f);
		check(handle != 0, "legal baseline: play returns a handle");

		float output[512 * 2];
		uint64_t hash = 1469598103934665603ULL;
		for (unsigned int block = 0; block < 8; block++)
		{
			soloud.mix(output, 512);
			uint64_t blockHash = hashFloatBuffer(output, 512 * 2);
			for (unsigned int i = 0; i < 8; i++)
			{
				hash ^= (blockHash >> (i * 8)) & 0xffu;
				hash *= 1099511628211ULL;
			}
		}
		soloud.deinit();
		return hash;
	}

}

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--legal-only") == 0)
	{
		uint64_t legalHash = testLegalOutputBaseline();
		printf("LEGAL_OUTPUT_HASH=%016llx\n", (unsigned long long)legalHash);
		check(legalHash == 0xdeeb2a71a2e4470eULL,
			"legal baseline: deterministic output hash is unchanged");
		printf("TEST_SUMMARY total=%d passed=%d failed=%d\n",
			gTests, gTests - gFailures, gFailures);
		return gFailures == 0 ? 0 : 1;
	}

	testZeroLengthInputs();
	testHugeSampleRates();
	testInvalidChannels();
	testOversizedBuffers();
	testRepeatedInitialization();
	testErrorStrings();

	uint64_t legalHash = testLegalOutputBaseline();
	printf("LEGAL_OUTPUT_HASH=%016llx\n", (unsigned long long)legalHash);
	check(legalHash == 0xdeeb2a71a2e4470eULL,
		"legal baseline: deterministic output hash is unchanged");

	int passed = gTests - gFailures;
	printf("TEST_SUMMARY total=%d passed=%d failed=%d\n", gTests, passed, gFailures);
	return gFailures == 0 ? 0 : 1;
}
