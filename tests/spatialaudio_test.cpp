#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soloud_spatialaudio.h"

static int gTests = 0;
static int gErrors = 0;
static unsigned long gAllocations = 0;

void *operator new(size_t aSize)
{
	gAllocations++;
	void *p = malloc(aSize ? aSize : 1);
	if (p == 0)
		abort();
	return p;
}

void *operator new[](size_t aSize)
{
	gAllocations++;
	void *p = malloc(aSize ? aSize : 1);
	if (p == 0)
		abort();
	return p;
}

void operator delete(void *aPtr)
{
	free(aPtr);
}

void operator delete[](void *aPtr)
{
	free(aPtr);
}

void operator delete(void *aPtr, size_t)
{
	free(aPtr);
}

void operator delete[](void *aPtr, size_t)
{
	free(aPtr);
}

#define CHECK(x) do { \
	gTests++; \
	if (!(x)) { \
		gErrors++; \
		printf("FAIL line %d: %s\n", __LINE__, #x); \
	} \
} while (0)

static bool finite(float aValue)
{
	return aValue == aValue &&
		aValue >= -3.402823466e+38f &&
		aValue <= 3.402823466e+38f;
}

static bool near(float aA, float aB, float aTolerance)
{
	if (!finite(aA) || !finite(aB))
		return false;
	return fabs(aA - aB) <= aTolerance;
}

static SoLoud::SpatialAudio::Scene makeScene(
	float aSourceX, float aSourceY, float aSourceZ,
	float aOcclusion)
{
	SoLoud::SpatialAudio::Scene scene;
	scene.listener.position = SoLoud::SpatialAudio::Vec3(0, 0, 0);
	scene.listener.forward = SoLoud::SpatialAudio::Vec3(0, 0, -1);
	scene.listener.up = SoLoud::SpatialAudio::Vec3(0, 1, 0);
	scene.source.position =
		SoLoud::SpatialAudio::Vec3(aSourceX, aSourceY, aSourceZ);
	scene.source.minDistance = 1;
	scene.source.maxDistance = 100;
	scene.source.rolloff = 1;
	scene.source.occlusion = aOcclusion;
	scene.source.distanceModel =
		SoLoud::SpatialAudio::INVERSE_DISTANCE;
	return scene;
}

static void testGeometryAndPanning()
{
	SoLoud::SpatialAudio::Listener listener;
	SoLoud::SpatialAudio::Source source;
	source.position = SoLoud::SpatialAudio::Vec3(0, 0, -1);

	SoLoud::SpatialAudio::Frame frame =
		SoLoud::SpatialAudio::calculateFrame(
			listener, source, 48000, 0.00066f);
	CHECK(near(frame.distance, 1, 0.00001f));
	CHECK(near(frame.azimuth, 0, 0.00001f));
	CHECK(near(frame.elevation, 0, 0.00001f));
	CHECK(near(frame.pan, 0, 0.00001f));
	CHECK(near(frame.gainLeft, 0.70710678f, 0.00001f));
	CHECK(near(frame.gainRight, 0.70710678f, 0.00001f));
	CHECK(frame.delayLeftSamples == 0);
	CHECK(frame.delayRightSamples == 0);

	source.position = SoLoud::SpatialAudio::Vec3(1, 0, 0);
	frame = SoLoud::SpatialAudio::calculateFrame(
		listener, source, 48000, 0.00066f);
	CHECK(near(frame.azimuth, 1.5707963f, 0.00001f));
	CHECK(near(frame.pan, 1, 0.00001f));
	CHECK(frame.gainRight > frame.gainLeft);
	CHECK(frame.delayLeftSamples > 0);
	CHECK(frame.delayRightSamples == 0);

	source.position = SoLoud::SpatialAudio::Vec3(-1, 0, 0);
	frame = SoLoud::SpatialAudio::calculateFrame(
		listener, source, 48000, 0.00066f);
	CHECK(near(frame.pan, -1, 0.00001f));
	CHECK(frame.gainLeft > frame.gainRight);
	CHECK(frame.delayLeftSamples == 0);
	CHECK(frame.delayRightSamples > 0);

	source.position = SoLoud::SpatialAudio::Vec3(0, 1, 0);
	frame = SoLoud::SpatialAudio::calculateFrame(
		listener, source, 48000, 0.00066f);
	CHECK(near(frame.elevation, 1.5707963f, 0.00001f));
	CHECK(near(frame.pan, 0, 0.00001f));
	CHECK(near(frame.gainLeft, frame.gainRight, 0.00001f));
	CHECK(frame.delayLeftSamples == 0);
	CHECK(frame.delayRightSamples == 0);

	source.position = SoLoud::SpatialAudio::Vec3(0, 0, 1);
	frame = SoLoud::SpatialAudio::calculateFrame(
		listener, source, 48000, 0.00066f);
	CHECK(near(frame.pan, 0, 0.00001f));
	CHECK(near(frame.gainLeft, frame.gainRight, 0.00001f));

	source.position = SoLoud::SpatialAudio::Vec3(0, 0, 0);
	frame = SoLoud::SpatialAudio::calculateFrame(
		listener, source, 48000, 0.00066f);
	CHECK(frame.distance == 0);
	CHECK(near(frame.gain, 1, 0.00001f));
	CHECK(finite(frame.gainLeft) && finite(frame.gainRight));

	SoLoud::SpatialAudio::Listener degenerate;
	degenerate.forward = SoLoud::SpatialAudio::Vec3(0, 0, 0);
	frame = SoLoud::SpatialAudio::calculateFrame(
		degenerate, source, 48000, 0.00066f);
	CHECK(finite(frame.gainLeft) && finite(frame.gainRight));

	degenerate.forward = SoLoud::SpatialAudio::Vec3(0, 1, 0);
	degenerate.up = SoLoud::SpatialAudio::Vec3(0, 1, 0);
	frame = SoLoud::SpatialAudio::calculateFrame(
		degenerate, source, 48000, 0.00066f);
	CHECK(finite(frame.gainLeft) && finite(frame.gainRight));
}

static void testDistanceModels()
{
	using namespace SoLoud;
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::NO_ATTENUATION, 1000, 1, 10, 1), 1, 0.00001f));
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::INVERSE_DISTANCE, 0, 1, 10, 1), 1, 0.00001f));
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::INVERSE_DISTANCE, 5, 1, 10, 1), 0.2f, 0.00001f));
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::INVERSE_DISTANCE, 10, 1, 10, 1), 0.1f, 0.00001f));
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::INVERSE_DISTANCE, 100, 1, 10, 1), 0.1f, 0.00001f));

	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::LINEAR_DISTANCE, 5, 1, 10, 1),
		5.0f / 9.0f, 0.00001f));
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::LINEAR_DISTANCE, 10, 1, 10, 1), 0, 0.00001f));
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::LINEAR_DISTANCE, 20, 1, 10, 1), 0, 0.00001f));
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::LINEAR_DISTANCE, 5, 1, 1, 1), 1, 0.00001f));

	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::EXPONENTIAL_DISTANCE, 10, 1, 10, 1),
		0.1f, 0.00001f));
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::EXPONENTIAL_DISTANCE, 0, 0, 10, 1), 1, 0.00001f));

	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::INVERSE_DISTANCE, -5, -1, -2, -1), 1, 0.00001f));
	CHECK(near(SpatialAudio::calculateDistanceGain(
		SpatialAudio::LINEAR_DISTANCE, 5, 1, 10, -1), 1, 0.00001f));
}

static void testValidation()
{
	using namespace SoLoud;
	SpatialAudio spatial;
	SpatialAudio::Scene scene = makeScene(0, 0, -5, 1);
	CHECK(spatial.switchScene(scene) == SO_NO_ERROR);
	unsigned int oldRate = spatial.getSampleRate();

	SpatialAudio::Source badSource = scene.source;
	badSource.minDistance = 0;
	CHECK(spatial.setSource(badSource) == INVALID_PARAMETER);
	CHECK(spatial.getTargetScene().source.minDistance == 1);

	badSource = scene.source;
	badSource.maxDistance = 0.5f;
	CHECK(spatial.setSource(badSource) == INVALID_PARAMETER);

	badSource = scene.source;
	badSource.rolloff = -1;
	CHECK(spatial.setSource(badSource) == INVALID_PARAMETER);

	badSource = scene.source;
	badSource.occlusion = 1.01f;
	CHECK(spatial.setSource(badSource) == INVALID_PARAMETER);

	badSource = scene.source;
	badSource.distanceModel = 99;
	CHECK(spatial.setSource(badSource) == INVALID_PARAMETER);

	badSource = scene.source;
	float zero = 0;
	badSource.position = SpatialAudio::Vec3(zero / zero, 0, 0);
	CHECK(spatial.setSource(badSource) == INVALID_PARAMETER);

	SpatialAudio::Listener badListener = scene.listener;
	badListener.forward = SpatialAudio::Vec3(0, 0, 0);
	CHECK(spatial.setListener(badListener) == INVALID_PARAMETER);
	CHECK(spatial.getTargetScene().listener.forward.z == -1);

	CHECK(spatial.setSmoothingTime(-1) == INVALID_PARAMETER);
	CHECK(spatial.setSmoothingTime(0.0f / 0.0f) == INVALID_PARAMETER);
	CHECK(spatial.init(0) == INVALID_PARAMETER);
	CHECK(spatial.getSampleRate() == oldRate);
	CHECK(spatial.init(48000, -1) == INVALID_PARAMETER);

	CHECK(spatial.init(192000, 0.00066f) == SO_NO_ERROR);
	CHECK(spatial.getSampleRate() == 192000);

	float sample = 0;
	float left;
	float right;
	CHECK(!spatial.processPlanar(0, &left, &right, 1));
	CHECK(!spatial.processPlanar(&sample, 0, &right, 1));
	CHECK(!spatial.process(0, 0, 1));
	CHECK(spatial.processPlanar(&sample, &left, &right, 0));
	CHECK(spatial.process(0, 0, 0) == false);
}

static void testContinuityAndBlockSize()
{
	using namespace SoLoud;
	const unsigned int count = 4096;
	float input[count];
	float left1[count];
	float right1[count];
	float left2[count];
	float right2[count];
	unsigned int i;
	for (i = 0; i < count; i++)
		input[i] = 0.5f;

	SpatialAudio::Scene sceneA = makeScene(-2, 0, 0, 1);
	SpatialAudio::Scene sceneB = makeScene(2, 0, 0, 0.2f);

	SpatialAudio single;
	SpatialAudio blocky;
	CHECK(single.init(48000) == SO_NO_ERROR);
	CHECK(blocky.init(48000) == SO_NO_ERROR);
	CHECK(single.setSmoothingTime(0.02f) == SO_NO_ERROR);
	CHECK(blocky.setSmoothingTime(0.02f) == SO_NO_ERROR);
	CHECK(single.switchScene(sceneA) == SO_NO_ERROR);
	CHECK(blocky.switchScene(sceneA) == SO_NO_ERROR);
	single.snapToTarget();
	blocky.snapToTarget();

	for (i = 0; i < count; i++)
	{
		if (i == 1024)
		{
			CHECK(single.switchScene(sceneB) == SO_NO_ERROR);
		}
		single.processPlanar(input + i, left1 + i, right1 + i, 1);
	}

	i = 0;
	while (i < count)
	{
		if (i == 1024)
		{
			CHECK(blocky.switchScene(sceneB) == SO_NO_ERROR);
		}
		unsigned int frames = 37;
		if (i < 1024 && i + frames > 1024)
			frames = 1024 - i;
		if (i + frames > count)
			frames = count - i;
		blocky.processPlanar(
			input + i, left2 + i, right2 + i, frames);
		i += frames;
	}

	float maxDifference = 0;
	unsigned int maxDifferenceAt = 0;
	for (i = 0; i < count; i++)
	{
		float dl = fabs(left1[i] - left2[i]);
		float dr = fabs(right1[i] - right2[i]);
		if (dl > maxDifference)
		{
			maxDifference = dl;
			maxDifferenceAt = i;
		}
		if (dr > maxDifference)
		{
			maxDifference = dr;
			maxDifferenceAt = i;
		}
	}
	if (maxDifference > 0.000001f)
		printf("block difference %.9f at %u: %.9f %.9f vs %.9f %.9f\n",
			maxDifference, maxDifferenceAt,
			left1[maxDifferenceAt], right1[maxDifferenceAt],
			left2[maxDifferenceAt], right2[maxDifferenceAt]);
	CHECK(maxDifference <= 0.000001f);

	SpatialAudio continuity;
	CHECK(continuity.init(48000) == SO_NO_ERROR);
	CHECK(continuity.setSmoothingTime(0.02f) == SO_NO_ERROR);
	CHECK(continuity.switchScene(sceneA) == SO_NO_ERROR);
	continuity.snapToTarget();

	float beforeLeft[512];
	float beforeRight[512];
	float afterLeft[512];
	float afterRight[512];
	continuity.processPlanar(
		input, beforeLeft, beforeRight, 512);
	CHECK(continuity.switchScene(sceneB) == SO_NO_ERROR);
	continuity.processPlanar(
		input, afterLeft, afterRight, 512);

	float maxStep = 0;
	for (i = 0; i < 512; i++)
	{
		if (i > 0)
		{
			float step = fabs(afterLeft[i] - afterLeft[i - 1]);
			if (step > maxStep)
				maxStep = step;
			step = fabs(afterRight[i] - afterRight[i - 1]);
			if (step > maxStep)
				maxStep = step;
		}
		if (!finite(afterLeft[i]) || !finite(afterRight[i]))
			maxStep = 1e30f;
	}
	CHECK(fabs(afterLeft[0] - beforeLeft[511]) < 0.01f);
	CHECK(fabs(afterRight[0] - beforeRight[511]) < 0.01f);
	CHECK(maxStep < 0.01f);

	CHECK(continuity.setSmoothingTime(0) == SO_NO_ERROR);
	CHECK(continuity.setOcclusion(0) == SO_NO_ERROR);
	continuity.processPlanar(input, afterLeft, afterRight, 1);
	CHECK(afterLeft[0] == 0);
	CHECK(afterRight[0] == 0);
}

static void testInterleavedOutput()
{
	using namespace SoLoud;
	SpatialAudio planar;
	SpatialAudio interleaved;
	SpatialAudio::Scene scene = makeScene(2, 1, -3, 0.75f);
	CHECK(planar.init(48000) == SO_NO_ERROR);
	CHECK(interleaved.init(48000) == SO_NO_ERROR);
	CHECK(planar.switchScene(scene) == SO_NO_ERROR);
	CHECK(interleaved.switchScene(scene) == SO_NO_ERROR);
	planar.snapToTarget();
	interleaved.snapToTarget();

	float input[3] = { 0.1f, -0.2f, 0.3f };
	float left[3];
	float right[3];
	float stereo[6];
	CHECK(planar.processPlanar(input, left, right, 3));
	CHECK(interleaved.process(input, stereo, 3));

	unsigned int i;
	for (i = 0; i < 3; i++)
	{
		CHECK(near(stereo[i * 2], left[i], 0.000001f));
		CHECK(near(stereo[i * 2 + 1], right[i], 0.000001f));
	}
}

static void testTenThousandUpdates()
{
	using namespace SoLoud;
	SpatialAudio spatial;
	CHECK(spatial.init(48000) == SO_NO_ERROR);
	CHECK(spatial.setSmoothingTime(0.01f) == SO_NO_ERROR);

	float input[4] = { 0.125f, -0.25f, 0.5f, -0.75f };
	float left[4];
	float right[4];
	spatial.processPlanar(input, left, right, 4);

	unsigned long allocationBaseline = gAllocations;
	unsigned int i;
	for (i = 0; i < 10000; i++)
	{
		SpatialAudio::Scene scene = makeScene(
			(float)(i % 17) - 8,
			(float)(i % 7) - 3,
			(float)(i % 23) - 11,
			(i & 1) ? 0.25f : 1.0f);
		scene.source.distanceModel =
			(i % 3) == 0 ? SpatialAudio::LINEAR_DISTANCE :
			(i % 3) == 1 ? SpatialAudio::EXPONENTIAL_DISTANCE :
			SpatialAudio::INVERSE_DISTANCE;
		scene.source.rolloff = (float)(i % 5) * 0.25f;
		scene.listener.forward =
			SpatialAudio::Vec3((float)(i % 3) - 1, 0, -1);

		if (spatial.switchScene(scene) != SO_NO_ERROR)
			gErrors++;
		spatial.processPlanar(input, left, right, 4);
		if (!finite(left[0]) || !finite(left[1]) ||
			!finite(left[2]) || !finite(left[3]) ||
			!finite(right[0]) || !finite(right[1]) ||
			!finite(right[2]) || !finite(right[3]))
		{
			gErrors++;
		}
	}

	gTests++;
	if (gAllocations != allocationBaseline)
	{
		gErrors++;
		printf("FAIL: %lu allocations during 10000 updates\n",
			gAllocations - allocationBaseline);
	}
	CHECK(spatial.getProcessedSamples() == 40004);
	CHECK(spatial.getCurrentFrame().gainLeft >= 0);
	CHECK(spatial.getCurrentFrame().gainLeft <= 1);
	CHECK(spatial.getCurrentFrame().gainRight >= 0);
	CHECK(spatial.getCurrentFrame().gainRight <= 1);
	CHECK(spatial.getCurrentFrame().delayLeftSamples >= 0);
	CHECK(spatial.getCurrentFrame().delayLeftSamples <
		SpatialAudio::MAX_DELAY_SAMPLES);
	CHECK(spatial.getCurrentFrame().delayRightSamples >= 0);
	CHECK(spatial.getCurrentFrame().delayRightSamples <
		SpatialAudio::MAX_DELAY_SAMPLES);
	CHECK(gErrors == 0);
}

static float cleanZero(float aValue)
{
	if (aValue > -0.0000005f && aValue < 0.0000005f)
		return 0;
	return aValue;
}

static int dumpPanning(const char *aPath)
{
	FILE *file = fopen(aPath, "w");
	if (file == 0)
		return 1;

	fprintf(file,
		"azimuth_deg,elevation_deg,pan,gain_left,gain_right,"
		"itd_left_ms,itd_right_ms\n");

	int azimuth;
	int elevation;
	for (elevation = -30; elevation <= 60; elevation += 15)
	{
		for (azimuth = -180; azimuth <= 180; azimuth += 10)
		{
			float az = (float)azimuth * 3.14159265359f / 180.0f;
			float el = (float)elevation * 3.14159265359f / 180.0f;
			SoLoud::SpatialAudio::Listener listener;
			SoLoud::SpatialAudio::Source source;
			source.position = SoLoud::SpatialAudio::Vec3(
				(float)sin(az) * (float)cos(el),
				(float)sin(el),
				-(float)cos(az) * (float)cos(el));
			SoLoud::SpatialAudio::Frame frame =
				SoLoud::SpatialAudio::calculateFrame(
					listener, source, 48000, 0.00066f);
			fprintf(file,
				"%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f\n",
				azimuth,
				elevation,
				cleanZero(frame.pan),
				cleanZero(frame.gainLeft),
				cleanZero(frame.gainRight),
				cleanZero(frame.delayLeftSeconds * 1000),
				cleanZero(frame.delayRightSeconds * 1000));
		}
	}
	fclose(file);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "--dump-panning") == 0)
		return dumpPanning(argv[2]);

	testGeometryAndPanning();
	testDistanceModels();
	testValidation();
	testContinuityAndBlockSize();
	testInterleavedOutput();
	testTenThousandUpdates();

	printf("%d checks, %d error(s)\n", gTests, gErrors);
	return gErrors == 0 ? 0 : 1;
}
