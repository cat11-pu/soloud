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

#include <math.h>
#include <float.h>
#include <string.h>
#include "soloud_spatialaudio.h"

namespace SoLoud
{
	namespace
	{
		const float SPATIAL_EPSILON = 0.000001f;
		const float SPATIAL_MIN_DISTANCE = 0.0001f;
		const float SPATIAL_MAX_ITD = 0.00066f;
		const float SPATIAL_PI = 3.14159265359f;

		bool isFinite(float aValue)
		{
			return aValue == aValue &&
				aValue >= -FLT_MAX &&
				aValue <= FLT_MAX;
		}

		float clamp(float aValue, float aMin, float aMax)
		{
			if (aValue < aMin)
				return aMin;
			if (aValue > aMax)
				return aMax;
			return aValue;
		}

		float length(const SpatialAudio::Vec3 &aValue)
		{
			return (float)sqrt(
				aValue.x * aValue.x +
				aValue.y * aValue.y +
				aValue.z * aValue.z);
		}

		SpatialAudio::Vec3 normalize(const SpatialAudio::Vec3 &aValue)
		{
			float len = length(aValue);
			if (len <= SPATIAL_EPSILON)
				return SpatialAudio::Vec3(0, 0, 0);
			return SpatialAudio::Vec3(
				aValue.x / len,
				aValue.y / len,
				aValue.z / len);
		}

		float dot(const SpatialAudio::Vec3 &aA,
			const SpatialAudio::Vec3 &aB)
		{
			return aA.x * aB.x + aA.y * aB.y + aA.z * aB.z;
		}

		SpatialAudio::Vec3 cross(const SpatialAudio::Vec3 &aA,
			const SpatialAudio::Vec3 &aB)
		{
			return SpatialAudio::Vec3(
				aA.y * aB.z - aA.z * aB.y,
				aA.z * aB.x - aA.x * aB.z,
				aA.x * aB.y - aA.y * aB.x);
		}

		SpatialAudio::Vec3 subtract(const SpatialAudio::Vec3 &aA,
			const SpatialAudio::Vec3 &aB)
		{
			return SpatialAudio::Vec3(
				aA.x - aB.x,
				aA.y - aB.y,
				aA.z - aB.z);
		}

		SpatialAudio::Vec3 sanitize(const SpatialAudio::Vec3 &aValue)
		{
			return SpatialAudio::Vec3(
				isFinite(aValue.x) ? aValue.x : 0,
				isFinite(aValue.y) ? aValue.y : 0,
				isFinite(aValue.z) ? aValue.z : 0);
		}
	}

	SpatialAudio::Vec3::Vec3() :
		x(0),
		y(0),
		z(0)
	{
	}

	SpatialAudio::Vec3::Vec3(float aX, float aY, float aZ) :
		x(aX),
		y(aY),
		z(aZ)
	{
	}

	SpatialAudio::Listener::Listener() :
		position(0, 0, 0),
		forward(0, 0, -1),
		up(0, 1, 0)
	{
	}

	SpatialAudio::Source::Source() :
		position(0, 0, 0),
		minDistance(1),
		maxDistance(100),
		rolloff(1),
		occlusion(1),
		distanceModel(INVERSE_DISTANCE)
	{
	}

	SpatialAudio::Scene::Scene()
	{
	}

	SpatialAudio::Frame::Frame() :
		distance(0),
		azimuth(0),
		elevation(0),
		pan(0),
		distanceGain(1),
		occlusionGain(1),
		gain(1),
		gainLeft(0.70710678f),
		gainRight(0.70710678f),
		delayLeftSeconds(0),
		delayRightSeconds(0),
		delayLeftSamples(0),
		delayRightSamples(0)
	{
	}

	SpatialAudio::SpatialAudio()
	{
		mSampleRate = 48000;
		mMaxItdSeconds = SPATIAL_MAX_ITD;
		mSmoothingSeconds = 0.02f;
		mSmoothingCoefficient = 0;
		mWriteIndex = 0;
		mProcessedSamples = 0;
		memset(mDelayLine, 0, sizeof(mDelayLine));
		updateSmoothingCoefficient();
		updateTargetFrame();
		snapToTarget();
	}

	result SpatialAudio::init(unsigned int aSampleRate, float aMaxItdSeconds)
	{
		if (aSampleRate == 0 ||
			!isFinite(aMaxItdSeconds) ||
			aMaxItdSeconds < 0 ||
			aMaxItdSeconds > 1)
		{
			return INVALID_PARAMETER;
		}

		float maxItd = clamp(aMaxItdSeconds, 0, SPATIAL_MAX_ITD);
		float maxDelay = maxItd * aSampleRate;
		if (maxDelay > MAX_DELAY_SAMPLES - 2)
		{
			maxItd = (MAX_DELAY_SAMPLES - 2) / (float)aSampleRate;
		}

		mSampleRate = aSampleRate;
		mMaxItdSeconds = maxItd;
		mWriteIndex = 0;
		mProcessedSamples = 0;
		memset(mDelayLine, 0, sizeof(mDelayLine));
		updateSmoothingCoefficient();
		updateTargetFrame();
		snapToTarget();
		return SO_NO_ERROR;
	}

	result SpatialAudio::validateScene(const Scene &aScene) const
	{
		const Vec3 &listenerPosition = aScene.listener.position;
		const Vec3 &listenerForward = aScene.listener.forward;
		const Vec3 &listenerUp = aScene.listener.up;
		const Vec3 &sourcePosition = aScene.source.position;

		if (!isFinite(listenerPosition.x) ||
			!isFinite(listenerPosition.y) ||
			!isFinite(listenerPosition.z) ||
			!isFinite(listenerForward.x) ||
			!isFinite(listenerForward.y) ||
			!isFinite(listenerForward.z) ||
			!isFinite(listenerUp.x) ||
			!isFinite(listenerUp.y) ||
			!isFinite(listenerUp.z) ||
			!isFinite(sourcePosition.x) ||
			!isFinite(sourcePosition.y) ||
			!isFinite(sourcePosition.z))
		{
			return INVALID_PARAMETER;
		}

		if (!isFinite(aScene.source.minDistance) ||
			!isFinite(aScene.source.maxDistance) ||
			!isFinite(aScene.source.rolloff) ||
			!isFinite(aScene.source.occlusion) ||
			aScene.source.minDistance <= 0 ||
			aScene.source.maxDistance < aScene.source.minDistance ||
			aScene.source.rolloff < 0 ||
			aScene.source.occlusion < 0 ||
			aScene.source.occlusion > 1 ||
			aScene.source.distanceModel > EXPONENTIAL_DISTANCE)
		{
			return INVALID_PARAMETER;
		}

		if (length(listenerForward) <= SPATIAL_EPSILON)
			return INVALID_PARAMETER;

		return SO_NO_ERROR;
	}

	result SpatialAudio::switchScene(const Scene &aScene)
	{
		result validation = validateScene(aScene);
		if (validation != SO_NO_ERROR)
			return validation;

		mScene = aScene;
		updateTargetFrame();
		return SO_NO_ERROR;
	}

	result SpatialAudio::setListener(const Listener &aListener)
	{
		Scene scene = mScene;
		scene.listener = aListener;
		return switchScene(scene);
	}

	result SpatialAudio::setSource(const Source &aSource)
	{
		Scene scene = mScene;
		scene.source = aSource;
		return switchScene(scene);
	}

	result SpatialAudio::setSourcePosition(float aX, float aY, float aZ)
	{
		Scene scene = mScene;
		scene.source.position = Vec3(aX, aY, aZ);
		return switchScene(scene);
	}

	result SpatialAudio::setDistanceModel(unsigned int aDistanceModel,
		float aRolloff)
	{
		Scene scene = mScene;
		scene.source.distanceModel = aDistanceModel;
		scene.source.rolloff = aRolloff;
		return switchScene(scene);
	}

	result SpatialAudio::setMinMaxDistance(float aMinDistance,
		float aMaxDistance)
	{
		Scene scene = mScene;
		scene.source.minDistance = aMinDistance;
		scene.source.maxDistance = aMaxDistance;
		return switchScene(scene);
	}

	result SpatialAudio::setOcclusion(float aOcclusion)
	{
		Scene scene = mScene;
		scene.source.occlusion = aOcclusion;
		return switchScene(scene);
	}

	result SpatialAudio::setSmoothingTime(float aSeconds)
	{
		if (!isFinite(aSeconds) || aSeconds < 0)
			return INVALID_PARAMETER;

		mSmoothingSeconds = aSeconds;
		updateSmoothingCoefficient();
		return SO_NO_ERROR;
	}

	void SpatialAudio::updateSmoothingCoefficient()
	{
		if (mSmoothingSeconds <= 0)
		{
			mSmoothingCoefficient = 1;
			return;
		}

		float samples = mSmoothingSeconds * (float)mSampleRate;
		if (samples <= 1)
			mSmoothingCoefficient = 1;
		else
			mSmoothingCoefficient = 1 - (float)exp(-1.0 / samples);
	}

	float SpatialAudio::calculateDistanceGain(unsigned int aDistanceModel,
		float aDistance, float aMinDistance, float aMaxDistance,
		float aRolloff)
	{
		if (!isFinite(aDistance) || aDistance < 0)
			aDistance = 0;
		if (!isFinite(aMinDistance) || aMinDistance < SPATIAL_MIN_DISTANCE)
			aMinDistance = SPATIAL_MIN_DISTANCE;
		if (!isFinite(aMaxDistance) || aMaxDistance < aMinDistance)
			aMaxDistance = aMinDistance;
		if (!isFinite(aRolloff) || aRolloff < 0)
			aRolloff = 0;

		float distance = clamp(aDistance, aMinDistance, aMaxDistance);
		float result = 1;

		switch (aDistanceModel)
		{
		case INVERSE_DISTANCE:
			result = aMinDistance /
				(aMinDistance + aRolloff * (distance - aMinDistance));
			break;
		case LINEAR_DISTANCE:
			if (aMaxDistance - aMinDistance > SPATIAL_EPSILON)
			{
				result = 1 - aRolloff *
					(distance - aMinDistance) /
					(aMaxDistance - aMinDistance);
			}
			break;
		case EXPONENTIAL_DISTANCE:
			result = (float)pow(distance / aMinDistance, -aRolloff);
			break;
		default:
		case NO_ATTENUATION:
			result = 1;
			break;
		}

		if (!isFinite(result))
			return 0;
		return clamp(result, 0, 1);
	}

	SpatialAudio::Frame SpatialAudio::calculateFrame(
		const Listener &aListener, const Source &aSource,
		unsigned int aSampleRate, float aMaxItdSeconds)
	{
		Frame frame;
		if (aSampleRate == 0)
			aSampleRate = 48000;
		if (!isFinite(aMaxItdSeconds) || aMaxItdSeconds < 0)
			aMaxItdSeconds = SPATIAL_MAX_ITD;
		aMaxItdSeconds = clamp(aMaxItdSeconds, 0, SPATIAL_MAX_ITD);

		Vec3 listenerPosition = sanitize(aListener.position);
		Vec3 sourcePosition = sanitize(aSource.position);
		Vec3 forward = normalize(sanitize(aListener.forward));
		if (length(forward) <= SPATIAL_EPSILON)
			forward = Vec3(0, 0, -1);

		Vec3 up = normalize(sanitize(aListener.up));
		Vec3 right = cross(forward, up);
		if (length(right) <= SPATIAL_EPSILON)
		{
			if (fabs(forward.y) < 0.999f)
				up = Vec3(0, 1, 0);
			else
				up = Vec3(0, 0, 1);
			right = cross(forward, up);
		}
		right = normalize(right);
		up = normalize(cross(right, forward));

		Vec3 delta = subtract(sourcePosition, listenerPosition);
		float distance = length(delta);
		if (distance <= SPATIAL_EPSILON)
		{
			delta = Vec3(0, 0, 0);
			distance = 0;
		}
		else
		{
			delta = Vec3(
				delta.x / distance,
				delta.y / distance,
				delta.z / distance);
		}

		float azimuth = 0;
		float elevation = 0;
		if (distance > SPATIAL_EPSILON)
		{
			azimuth = (float)atan2(dot(delta, right), dot(delta, forward));
			elevation = (float)asin(clamp(dot(delta, up), -1, 1));
		}

		float pan = clamp(
			(float)sin(azimuth) * (float)cos(elevation),
			-1, 1);
		if (fabs(pan) < SPATIAL_EPSILON)
			pan = 0;

		float angle = (pan + 1) * SPATIAL_PI * 0.25f;
		float gainLeft = clamp((float)cos(angle), 0, 1);
		float gainRight = clamp((float)sin(angle), 0, 1);

		float distanceGain = calculateDistanceGain(
			aSource.distanceModel,
			distance,
			aSource.minDistance,
			aSource.maxDistance,
			aSource.rolloff);
		float occlusionGain = isFinite(aSource.occlusion) ?
			clamp(aSource.occlusion, 0, 1) : 0;
		float gain = distanceGain * occlusionGain;

		float itdSeconds = aMaxItdSeconds * fabs(pan);
		float itdSamples = itdSeconds * (float)aSampleRate;
		if (itdSamples > SpatialAudio::MAX_DELAY_SAMPLES - 2)
			itdSamples = SpatialAudio::MAX_DELAY_SAMPLES - 2;
		itdSeconds = itdSamples / (float)aSampleRate;

		frame.distance = distance;
		frame.azimuth = azimuth;
		frame.elevation = elevation;
		frame.pan = pan;
		frame.distanceGain = distanceGain;
		frame.occlusionGain = occlusionGain;
		frame.gain = gain;
		frame.gainLeft = gainLeft * gain;
		frame.gainRight = gainRight * gain;
		frame.delayLeftSamples = pan > 0 ? itdSamples : 0;
		frame.delayRightSamples = pan < 0 ? itdSamples : 0;
		frame.delayLeftSeconds =
			frame.delayLeftSamples / (float)aSampleRate;
		frame.delayRightSeconds =
			frame.delayRightSamples / (float)aSampleRate;
		return frame;
	}

	void SpatialAudio::updateTargetFrame()
	{
		mTarget = calculateFrame(
			mScene.listener,
			mScene.source,
			mSampleRate,
			mMaxItdSeconds);
	}

	void SpatialAudio::snapToTarget()
	{
		mCurrent = mTarget;
	}

	void SpatialAudio::reset()
	{
		memset(mDelayLine, 0, sizeof(mDelayLine));
		mWriteIndex = 0;
		mProcessedSamples = 0;
		snapToTarget();
	}

	float SpatialAudio::readDelayed(float aDelaySamples) const
	{
		float delay = clamp(aDelaySamples, 0, MAX_DELAY_SAMPLES - 2);
		unsigned int whole = (unsigned int)delay;
		float fraction = delay - whole;
		unsigned int index0 =
			(mWriteIndex + MAX_DELAY_SAMPLES - whole) % MAX_DELAY_SAMPLES;
		unsigned int index1 =
			(index0 + MAX_DELAY_SAMPLES - 1) % MAX_DELAY_SAMPLES;
		float sample0 = mDelayLine[index0];
		float sample1 = mDelayLine[index1];
		return sample0 + (sample1 - sample0) * fraction;
	}

	void SpatialAudio::processSample(float aInput,
		float &aOutputLeft, float &aOutputRight)
	{
		if (!isFinite(aInput))
			aInput = 0;

		mDelayLine[mWriteIndex] = aInput;

		mCurrent.gainLeft +=
			(mTarget.gainLeft - mCurrent.gainLeft) *
			mSmoothingCoefficient;
		mCurrent.gainRight +=
			(mTarget.gainRight - mCurrent.gainRight) *
			mSmoothingCoefficient;
		mCurrent.delayLeftSamples +=
			(mTarget.delayLeftSamples - mCurrent.delayLeftSamples) *
			mSmoothingCoefficient;
		mCurrent.delayRightSamples +=
			(mTarget.delayRightSamples - mCurrent.delayRightSamples) *
			mSmoothingCoefficient;

		aOutputLeft = readDelayed(mCurrent.delayLeftSamples) *
			mCurrent.gainLeft;
		aOutputRight = readDelayed(mCurrent.delayRightSamples) *
			mCurrent.gainRight;

		if (!isFinite(aOutputLeft))
			aOutputLeft = 0;
		if (!isFinite(aOutputRight))
			aOutputRight = 0;

		mWriteIndex = (mWriteIndex + 1) % MAX_DELAY_SAMPLES;
		mProcessedSamples++;
	}

	void SpatialAudio::syncCurrentMetadata()
	{
		mCurrent.distance = mTarget.distance;
		mCurrent.azimuth = mTarget.azimuth;
		mCurrent.elevation = mTarget.elevation;
		mCurrent.pan = mTarget.pan;
		mCurrent.distanceGain = mTarget.distanceGain;
		mCurrent.occlusionGain = mTarget.occlusionGain;
		mCurrent.gain = mTarget.gain;
		mCurrent.delayLeftSeconds = mCurrent.delayLeftSamples /
			(float)mSampleRate;
		mCurrent.delayRightSeconds = mCurrent.delayRightSamples /
			(float)mSampleRate;
	}

	bool SpatialAudio::processPlanar(const float *aInput,
		float *aOutputLeft, float *aOutputRight, unsigned int aFrames)
	{
		if (aInput == 0 || aOutputLeft == 0 || aOutputRight == 0)
			return false;

		unsigned int i;
		for (i = 0; i < aFrames; i++)
			processSample(aInput[i], aOutputLeft[i], aOutputRight[i]);
		syncCurrentMetadata();
		return true;
	}

	bool SpatialAudio::process(const float *aInput, float *aOutput,
		unsigned int aFrames)
	{
		if (aInput == 0 || aOutput == 0)
			return false;

		unsigned int i;
		for (i = 0; i < aFrames; i++)
		{
			float left = 0;
			float right = 0;
			processSample(aInput[i], left, right);
			aOutput[i * 2] = left;
			aOutput[i * 2 + 1] = right;
		}
		syncCurrentMetadata();
		return true;
	}

	const SpatialAudio::Frame &SpatialAudio::getCurrentFrame() const
	{
		return mCurrent;
	}

	const SpatialAudio::Frame &SpatialAudio::getTargetFrame() const
	{
		return mTarget;
	}

	const SpatialAudio::Scene &SpatialAudio::getTargetScene() const
	{
		return mScene;
	}

	unsigned int SpatialAudio::getSampleRate() const
	{
		return mSampleRate;
	}

	unsigned int SpatialAudio::getProcessedSamples() const
	{
		return mProcessedSamples;
	}
}
