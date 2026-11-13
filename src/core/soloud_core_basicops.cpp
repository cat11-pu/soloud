/*
SoLoud audio engine
Copyright (c) 2013-2015 Jari Komppa

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

#include <string.h>
#include "soloud_internal.h"

// Core "basic" operations - play, stop, etc

namespace SoLoud
{
	handle Soloud::play(AudioSource &aSound, float aVolume, float aPan, bool aPaused, unsigned int aBus)
	{
		if (aSound.mFlags & AudioSource::SINGLE_INSTANCE)
		{
			// Only one instance allowed, stop others
			aSound.stop();
		}

		// Creation of an audio instance may take significant amount of time,
		// so let's not do it inside the audio thread mutex.
		aSound.mSoloud = this;
		SoLoud::AudioSourceInstance *instance = aSound.createInstance();

		lockAudioMutex_internal();
		int ch = findFreeVoice_internal();
		if (ch < 0) 
		{
			unlockAudioMutex_internal();
			delete instance;
			return UNKNOWN_ERROR;
		}
		if (!aSound.mAudioSourceID)
		{
			aSound.mAudioSourceID = mAudioSourceID;
			mAudioSourceID++;
		}
		mVoice[ch] = instance;
		mVoice[ch]->mAudioSourceID = aSound.mAudioSourceID;
		mVoice[ch]->mBusHandle = aBus;
		mVoice[ch]->init(aSound, mPlayIndex);
		m3dData[ch].init(aSound);

		mPlayIndex++;

		// 20 bits, skip the last one (top bits full = voice group)
		if (mPlayIndex == 0xfffff) 
		{
			mPlayIndex = 0;
		}

		if (aPaused)
		{
			mVoice[ch]->mFlags |= AudioSourceInstance::PAUSED;
		}

		setVoicePan_internal(ch, aPan);
		if (aVolume < 0)
		{
			setVoiceVolume_internal(ch, aSound.mVolume);
		}
		else
		{
			setVoiceVolume_internal(ch, aVolume);
		}

		// Keep the pan/volume interpolation state matched with the target
		// channel volumes so the only start-up transient is the explicit
		// fade-in envelope below.
		int i;
		for (i = 0; i < MAX_CHANNELS; i++)
		{
			mVoice[ch]->mCurrentChannelVolume[i] = mVoice[ch]->mChannelVolume[i] * mVoice[ch]->mOverallVolume;
		}

		// Millisecond-resolution fade-in. Starting the envelope at gain 0
		// guarantees a re-triggered voice cannot inject a DC step/offset.
		mVoice[ch]->mAttackSamplesDone = 0;
		mVoice[ch]->mAttackSamplesTotal =
			(unsigned int)((double)mVoiceFadeInMs * 0.001 * (double)mSamplerate);
		mVoice[ch]->mReleaseSamplesDone = 0;
		mVoice[ch]->mReleaseSamplesTotal = 0;
		mVoice[ch]->mEnvelopeGain = 1.0f;
		mVoice[ch]->mEnvelopeGainStart = 1.0f;

		setVoiceRelativePlaySpeed_internal(ch, 1);
		
		for (i = 0; i < FILTERS_PER_STREAM; i++)
		{
			if (aSound.mFilter[i])
			{
				mVoice[ch]->mFilter[i] = aSound.mFilter[i]->createInstance();
			}
		}

		mActiveVoiceDirty = true;

		unlockAudioMutex_internal();

		int handle = getHandleFromVoice_internal(ch);
		return handle;
	}

	handle Soloud::playClocked(time aSoundTime, AudioSource &aSound, float aVolume, float aPan, unsigned int aBus)
	{
		handle h = play(aSound, aVolume, aPan, 1, aBus);
		lockAudioMutex_internal();
		// mLastClockedTime is cleared to zero at start of every output buffer
		time lasttime = mLastClockedTime;
		if (lasttime == 0)
		{
			mLastClockedTime = aSoundTime;
			lasttime = aSoundTime;
		}
		unlockAudioMutex_internal();
		int samples = (int)floor((aSoundTime - lasttime) * mSamplerate);
		// Make sure we don't delay too much (or overflow)
		if (samples < 0 || samples > 2048)		
			samples = 0;
		setDelaySamples(h, samples);
		setPause(h, 0);
		return h;
	}

	handle Soloud::playBackground(AudioSource &aSound, float aVolume, bool aPaused, unsigned int aBus)
	{
		handle h = play(aSound, aVolume, 0.0f, aPaused, aBus);
		setPanAbsolute(h, 1.0f, 1.0f);
		return h;
	}

	result Soloud::seek(handle aVoiceHandle, time aSeconds)
	{
		result res = SO_NO_ERROR;
		result singleres = SO_NO_ERROR;
		FOR_ALL_VOICES_PRE
			singleres = mVoice[ch]->seek(aSeconds, mScratch.mData, mScratchSize);
		if (singleres != SO_NO_ERROR)
			res = singleres;
		FOR_ALL_VOICES_POST
		return res;
	}


	void Soloud::stop(handle aVoiceHandle)
	{
		FOR_ALL_VOICES_PRE
			stopVoice_internal(ch);
		FOR_ALL_VOICES_POST
	}

	void Soloud::stop(handle aVoiceHandle, time aFadeoutTime)
	{
		unsigned int fadeSamples = 0;
		if (aFadeoutTime > 0 && mSamplerate > 0)
		{
			double s = aFadeoutTime * (double)mSamplerate;
			fadeSamples = (s > 0xffffffff) ? 0xffffffff : (unsigned int)s;
		}

		if (fadeSamples == 0)
		{
			stop(aVoiceHandle);
			return;
		}

		FOR_ALL_VOICES_PRE
			scheduleStopVoice_internal(ch, fadeSamples);
		FOR_ALL_VOICES_POST
	}

	void Soloud::stopAll(time aFadeoutTime)
	{
		unsigned int fadeSamples = 0;
		if (aFadeoutTime > 0 && mSamplerate > 0)
		{
			double s = aFadeoutTime * (double)mSamplerate;
			fadeSamples = (s > 0xffffffff) ? 0xffffffff : (unsigned int)s;
		}

		lockAudioMutex_internal();
		int i;
		for (i = 0; i < (signed)mHighestVoice; i++)
		{
			if (mVoice[i])
			{
				if (fadeSamples == 0)
					stopVoice_internal(i);
				else
					scheduleStopVoice_internal(i, fadeSamples);
			}
		}
		unlockAudioMutex_internal();
	}

	void Soloud::stopAudioSource(AudioSource &aSound)
	{
		if (aSound.mAudioSourceID)
		{
			lockAudioMutex_internal();
			
			int i;
			for (i = 0; i < (signed)mHighestVoice; i++)
			{
				if (mVoice[i] && mVoice[i]->mAudioSourceID == aSound.mAudioSourceID)
				{
					stopVoice_internal(i);
				}
			}
			unlockAudioMutex_internal();
		}
	}

	void Soloud::stopAll()
	{
		int i;
		lockAudioMutex_internal();
		for (i = 0; i < (signed)mHighestVoice; i++)
		{
			stopVoice_internal(i);
		}
		unlockAudioMutex_internal();
	}

	int Soloud::countAudioSource(AudioSource &aSound)
	{
		int count = 0;
		if (aSound.mAudioSourceID)
		{
			lockAudioMutex_internal();

			int i;
			for (i = 0; i < (signed)mHighestVoice; i++)
			{
				if (mVoice[i] && mVoice[i]->mAudioSourceID == aSound.mAudioSourceID)
				{
					count++;
				}
			}
			unlockAudioMutex_internal();
		}
		return count;
	}

}
