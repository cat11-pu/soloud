#include "robust_manager.h"

namespace fitest {

RobustManager::RobustManager(unsigned int sample_rate, unsigned int channels,
                             unsigned int capacity_frames)
    : dev_(nullptr),
      sample_rate_(sample_rate),
      channels_(channels),
      capacity_(capacity_frames),
      recovered_(0),
      swallowed_(0)
{
}

RobustManager::~RobustManager()
{
    // Deterministic release; destructor never throws.
    teardown_locked();
}

void RobustManager::teardown_locked()
{
    if (dev_) {
        dev_->close();
        delete dev_;
        dev_ = nullptr;
    }
}

OpResult RobustManager::open()
{
    // Idempotent: release any previous (possibly failed) instance first so
    // an open after a fault cannot double-allocate or leak.
    teardown_locked();

    dev_ = make_device(sample_rate_, channels_, capacity_);
    if (!dev_->is_open()) {
        // open failed before any allocation succeeded; stay closed.
        teardown_locked();
        return OpResult{false, true, "open rejected (device unavailable)"};
    }
    return OpResult{true, true, nullptr};
}

OpResult RobustManager::close()
{
    teardown_locked();
    return OpResult{true, true, nullptr};
}

OpResult RobustManager::play(unsigned int frames)
{
    if (!dev_ || !dev_->is_open())
        return OpResult{false, false, "play on closed device"};

    unsigned int before = dev_->overruns();
    unsigned int accepted = dev_->queue_frames(frames);
    if (dev_->overruns() > before && accepted < frames) {
        // Overflow is contained: caller is told data was dropped, but the
        // device stays usable for subsequent frames.
        return OpResult{true, true, "overrun: excess frames dropped"};
    }
    return OpResult{true, true, nullptr};
}

OpResult RobustManager::tick(unsigned int want)
{
    if (!dev_ || !dev_->is_open())
        return OpResult{false, false, "tick on closed device"};

    FaultyDevice::CallbackResult r = dev_->callback(want);

    if (r.timed_out) {
        // Missed deadline: tear the stream down and flag it for recovery so
        // the state never silently diverges from the hardware.
        teardown_locked();
        ++recovered_;
        return OpResult{false, true, "callback timeout: stream recovered"};
    }
    if (r.shortfall > 0) {
        // Underrun is a soft, recoverable condition; the device stays open.
        return OpResult{true, true, "underrun: silence inserted"};
    }
    return OpResult{true, true, nullptr};
}

OpResult RobustManager::hotplug()
{
    if (!dev_ || !dev_->is_open())
        return OpResult{false, false, "hotplug on closed device"};

    dev_->unplug();
    // Orderly teardown of the removed device; next open() re-acquires it.
    teardown_locked();
    ++recovered_;
    return OpResult{false, true, "device removed: torn down, reopen required"};
}

OpResult RobustManager::reset()
{
    teardown_locked();
    OpResult r = open();
    if (r.ok)
        ++recovered_;
    return r;
}

bool RobustManager::is_usable() const
{
    return dev_ && dev_->is_open();
}

} // namespace fitest
