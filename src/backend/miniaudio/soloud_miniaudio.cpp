/*
SoLoud audio engine
Copyright (c) 2013-2020 Jari Komppa

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
#include <stdlib.h>

#include "soloud.h"

#if !defined(WITH_MINIAUDIO)

namespace SoLoud
{
    result miniaudio_init(SoLoud::Soloud *aSoloud, unsigned int aFlags, unsigned int aSamplerate, unsigned int aBuffer, unsigned int aChannels)
    {
        return NOT_IMPLEMENTED;
    }
}

#else

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_NULL
#define MA_NO_DECODING
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#include "miniaudio.h"
#include <math.h>
#include "soloud_thread.h"

namespace SoLoud
{
    // Per-instance backend state. A global device made multiple engines
    // corrupt each other's state and allowed double-uninitialization.
    struct MiniaudioData
    {
        ma_device mDevice;
        ma_device_config mConfig;
        SoLoud::Soloud *mSoloud;
        Thread::ThreadHandle mWatchdog;
        void *mDeviceMutex;
        volatile bool mRunning;
        volatile bool mWantStarted;
        volatile bool mDeviceUp;

        MiniaudioData()
        {
            mSoloud = 0;
            mWatchdog = 0;
            mDeviceMutex = 0;
            mRunning = false;
            mWantStarted = false;
            mDeviceUp = false;
        }
    };

    void soloud_miniaudio_audiomixer(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
    {
        SoLoud::Soloud *soloud = (SoLoud::Soloud *)pDevice->pUserData;
        soloud->mix((float *)pOutput, frameCount);
    }

    // Recreate the playback device. Caller must hold mDeviceMutex.
    static void soloud_miniaudio_recover(MiniaudioData *data)
    {
        if (data->mDeviceUp)
        {
            ma_device_uninit(&data->mDevice);
            data->mDeviceUp = false;
        }
        if (ma_device_init(NULL, &data->mConfig, &data->mDevice) == MA_SUCCESS)
        {
            data->mDeviceUp = true;
            ma_device_start(&data->mDevice);
        }
    }

    // Polls the playback device. If it was stopped by the OS (for example
    // because the output device was unplugged), keep trying to recreate it
    // so audio resumes automatically once a device is available again.
    static void soloud_miniaudio_watchdog(void *aParam)
    {
        MiniaudioData *data = static_cast<MiniaudioData*>(aParam);
        while (data->mRunning)
        {
            if (data->mWantStarted)
            {
                Thread::lockMutex(data->mDeviceMutex);
                if (data->mDeviceUp)
                {
                    if (!ma_device_is_started(&data->mDevice) &&
                        ma_device_start(&data->mDevice) != MA_SUCCESS)
                    {
                        soloud_miniaudio_recover(data);
                    }
                }
                else
                {
                    soloud_miniaudio_recover(data);
                }
                Thread::unlockMutex(data->mDeviceMutex);
            }
            Thread::sleep(100);
        }
    }

    static void soloud_miniaudio_deinit(SoLoud::Soloud *aSoloud)
    {
        MiniaudioData *data = static_cast<MiniaudioData*>(aSoloud->mBackendData);
        if (data == 0)
            return;

        data->mWantStarted = false;
        data->mRunning = false;
        if (data->mWatchdog)
        {
            Thread::wait(data->mWatchdog);
            Thread::release(data->mWatchdog);
            data->mWatchdog = 0;
        }

        Thread::lockMutex(data->mDeviceMutex);
        if (data->mDeviceUp)
        {
            ma_device_uninit(&data->mDevice);
            data->mDeviceUp = false;
        }
        Thread::unlockMutex(data->mDeviceMutex);
        Thread::destroyMutex(data->mDeviceMutex);
        delete data;
        aSoloud->mBackendData = 0;
    }

    static result soloud_miniaudio_pause(SoLoud::Soloud *aSoloud)
    {
        MiniaudioData *data = static_cast<MiniaudioData*>(aSoloud->mBackendData);
        if (data == 0)
            return UNKNOWN_ERROR;

        data->mWantStarted = false;
        Thread::lockMutex(data->mDeviceMutex);
        if (data->mDeviceUp)
            ma_device_stop(&data->mDevice);
        Thread::unlockMutex(data->mDeviceMutex);
        return SO_NO_ERROR;
    }

    static result soloud_miniaudio_resume(SoLoud::Soloud *aSoloud)
    {
        MiniaudioData *data = static_cast<MiniaudioData*>(aSoloud->mBackendData);
        if (data == 0)
            return UNKNOWN_ERROR;

        Thread::lockMutex(data->mDeviceMutex);
        if (data->mDeviceUp)
            ma_device_start(&data->mDevice);
        Thread::unlockMutex(data->mDeviceMutex);
        // The watchdog retries if the device is still unavailable.
        data->mWantStarted = true;
        return SO_NO_ERROR;
    }

    result miniaudio_init(SoLoud::Soloud *aSoloud, unsigned int aFlags, unsigned int aSamplerate, unsigned int aBuffer, unsigned int aChannels)
    {
        if (aSamplerate < SOLOUD_MIN_SAMPLERATE || aSamplerate > SOLOUD_MAX_SAMPLERATE ||
            aChannels == 0 || aChannels > MAX_CHANNELS)
            return INVALID_PARAMETER;

        MiniaudioData *data = new MiniaudioData;
        data->mSoloud = aSoloud;
        data->mDeviceMutex = Thread::createMutex();
        if (data->mDeviceMutex == 0)
        {
            delete data;
            return OUT_OF_MEMORY;
        }

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        //config.periodSizeInFrames = aBuffer; // setting to aBuffer (like 2048) causes miniaudio to crash; let's just use the default.
        config.playback.format    = ma_format_f32;
        config.playback.channels  = aChannels;
        config.sampleRate         = aSamplerate;
        config.dataCallback       = soloud_miniaudio_audiomixer;
        config.pUserData          = (void *)aSoloud;
        data->mConfig             = config;

        if (ma_device_init(NULL, &config, &data->mDevice) != MA_SUCCESS)
        {
            Thread::destroyMutex(data->mDeviceMutex);
            delete data;
            return UNKNOWN_ERROR;
        }
        data->mDeviceUp = true;

        aSoloud->mBackendData = data;
        aSoloud->postinit_internal(data->mDevice.sampleRate, data->mDevice.playback.internalPeriodSizeInFrames, aFlags, data->mDevice.playback.channels);

        aSoloud->mBackendCleanupFunc = soloud_miniaudio_deinit;
        aSoloud->mBackendPauseFunc = soloud_miniaudio_pause;
        aSoloud->mBackendResumeFunc = soloud_miniaudio_resume;

        ma_device_start(&data->mDevice);
        data->mWantStarted = true;
        data->mRunning = true;
        data->mWatchdog = Thread::createThread(soloud_miniaudio_watchdog, data);
        if (data->mWatchdog == 0)
            data->mRunning = false;
        aSoloud->mBackendString = "MiniAudio";
        return 0;
    }
};
#endif
