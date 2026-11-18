/*
SoLoud audio engine

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#ifndef SOLOUD_SPATIALAUDIO_H
#define SOLOUD_SPATIALAUDIO_H

#include "soloud.h"

namespace SoLoud
{
	// Sample-accurate positional audio for a mono source.
	//
	// The processor uses a fixed-size delay line and performs no allocation
	// after construction. All setters publish target parameters; process()
	// moves the current gains and interaural delay toward those targets one
	// sample at a time. Calls must be serialized with process() if used from
	// different threads.
	class SpatialAudio
	{
	public:
		enum DISTANCE_MODELS
		{
			// No distance attenuation.
			NO_ATTENUATION = 0,
			// minDistance / (minDistance + rolloff * (distance - minDistance))
			INVERSE_DISTANCE = 1,
			// 1 - rolloff * (distance - minDistance) / (maxDistance - minDistance)
			LINEAR_DISTANCE = 2,
			// pow(distance / minDistance, -rolloff)
			EXPONENTIAL_DISTANCE = 3
		};

		enum
		{
			// Enough for 0.66 ms ITD at sample rates up to 384 kHz.
			MAX_DELAY_SAMPLES = 256
		};

		struct Vec3
		{
			float x;
			float y;
			float z;

			Vec3();
			Vec3(float aX, float aY, float aZ);
		};

		struct Listener
		{
			Vec3 position;
			Vec3 forward;
			Vec3 up;

			Listener();
		};

		struct Source
		{
			Vec3 position;
			float minDistance;
			float maxDistance;
			float rolloff;
			float occlusion;
			unsigned int distanceModel;

			Source();
		};

		struct Scene
		{
			Listener listener;
			Source source;

			Scene();
		};

		struct Frame
		{
			// Geometry at the source position.
			float distance;
			float azimuth;
			float elevation;
			float pan;

			// Attenuation terms.
			float distanceGain;
			float occlusionGain;
			float gain;

			// Constant-power stereo gains, already multiplied by gain.
			float gainLeft;
			float gainRight;

			// Interaural time difference. The near ear has zero delay.
			float delayLeftSeconds;
			float delayRightSeconds;
			float delayLeftSamples;
			float delayRightSamples;

			Frame();
		};

		SpatialAudio();

		// Set the render sample rate and maximum ITD. Calling init resets the
		// processor and delay line, and snaps the current state to the target.
		result init(unsigned int aSampleRate = 48000,
			float aMaxItdSeconds = 0.00066f);

		// Publish a complete listener/source state. The transition is smoothed
		// by process(); the delay line and current gains are not reset.
		result switchScene(const Scene &aScene);
		result setListener(const Listener &aListener);
		result setSource(const Source &aSource);
		result setSourcePosition(float aX, float aY, float aZ);
		result setDistanceModel(unsigned int aDistanceModel, float aRolloff);
		result setMinMaxDistance(float aMinDistance, float aMaxDistance);
		result setOcclusion(float aOcclusion);

		// Time constant of the per-sample one-pole transition, in seconds.
		// Zero means immediate parameter changes and is intended only for
		// initialization or an explicit hard cut.
		result setSmoothingTime(float aSeconds);

		// Mono input to interleaved stereo output. The input and output
		// buffers must not overlap.
		bool process(const float *aInput, float *aOutput, unsigned int aFrames);

		// Mono input to separate stereo output channels.
		bool processPlanar(const float *aInput, float *aOutputLeft,
			float *aOutputRight, unsigned int aFrames);

		// Snap current gains and delay to the latest targets without clearing
		// the delay line. Useful only before rendering starts.
		void snapToTarget();

		// Clear the delay line and snap current state to the latest targets.
		// This is a hard discontinuity and is not used by switchScene().
		void reset();

		// Current (smoothed) and target parameters. These are useful for
		// telemetry, tests, and generating panning tables.
		const Frame &getCurrentFrame() const;
		const Frame &getTargetFrame() const;
		const Scene &getTargetScene() const;

		unsigned int getSampleRate() const;
		unsigned int getProcessedSamples() const;

		// Distance attenuation helper. Invalid combinations are sanitized:
		// minDistance is at least 1e-4, maxDistance is at least minDistance,
		// rolloff is non-negative, and the result is clamped to [0, 1].
		static float calculateDistanceGain(unsigned int aDistanceModel,
			float aDistance, float aMinDistance, float aMaxDistance,
			float aRolloff);

		// Geometry and panning helper using the same conventions as process().
		static Frame calculateFrame(const Listener &aListener,
			const Source &aSource, unsigned int aSampleRate,
			float aMaxItdSeconds);

	private:
		result validateScene(const Scene &aScene) const;
		void updateTargetFrame();
		void updateSmoothingCoefficient();
		float readDelayed(float aDelaySamples) const;
		void processSample(float aInput, float &aOutputLeft,
			float &aOutputRight);
		void syncCurrentMetadata();

		unsigned int mSampleRate;
		float mMaxItdSeconds;
		float mSmoothingSeconds;
		float mSmoothingCoefficient;

		Scene mScene;
		Frame mTarget;
		Frame mCurrent;

		float mDelayLine[MAX_DELAY_SAMPLES];
		unsigned int mWriteIndex;
		unsigned int mProcessedSamples;
	};
}

#endif
