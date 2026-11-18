#include "buggy_manager.h"

namespace fitest {

BuggyManager::BuggyManager(unsigned int sample_rate, unsigned int channels,
                           unsigned int capacity_frames)
    : dev_(nullptr),
      sample_rate_(sample_rate),
      channels_(channels),
      capacity_(capacity_frames),
      recovered_(0),
      swallowed_(0),
      believe_open_(false)
{
}

BuggyManager::~BuggyManager()
{
    // Only the *current* pointer is released. Every B1 leak already lost the
    // previous allocation, so the destructor cannot reclaim it.
    delete dev_;
    dev_ = nullptr;
}

OpResult BuggyManager::open()
{
    // B1: if a pointer already exists it is overwritten without being
    // released. After a failed open or an unplug this leaks the old device.
    FaultyDevice *fresh = make_device(sample_rate_, channels_, capacity_);
    if (!fresh->is_open()) {
        // Failed open: the fresh device is abandoned entirely (leak) and the
        // cached "open" flag is left untouched.
        dev_ = fresh; // still reachable here but never closed on next open()
        believe_open_ = false;
        return OpResult{false, false, "open failed"};
    }
    dev_ = fresh;          // old pointer (if any) is leaked here
    believe_open_ = true;
    return OpResult{true, true, nullptr};
}

OpResult BuggyManager::close()
{
    if (dev_) {
        dev_->close();
        delete dev_;
        dev_ = nullptr;
    }
    believe_open_ = false;
    return OpResult{true, true, nullptr};
}

OpResult BuggyManager::play(unsigned int frames)
{
    // B2: trusts the cached flag instead of the real device state.
    if (!believe_open_)
        return OpResult{false, false, "play on closed device"};
    dev_->queue_frames(frames);
    return OpResult{true, true, nullptr};
}

OpResult BuggyManager::tick(unsigned int want)
{
    if (!believe_open_)
        return OpResult{false, false, "tick on closed device"};

    FaultyDevice::CallbackResult r = dev_->callback(want);
    if (r.timed_out) {
        // B3: swallow the timeout and claim success.
        ++swallowed_;
        return OpResult{true, true, nullptr};
    }
    if (r.shortfall > 0)
        return OpResult{true, true, "underrun (ignored)"};
    return OpResult{true, true, nullptr};
}

OpResult BuggyManager::hotplug()
{
    if (!dev_ || !believe_open_)
        return OpResult{false, false, "hotplug on closed device"};

    dev_->unplug();
    // B1 + B2: the removed device is neither closed nor deleted, and the
    // cached flag still claims the device is open. The allocation is lost on
    // the next open(), and play()/tick() now operate on a dead device.
    believe_open_ = true;
    return OpResult{false, false, "device removed (state left stale)"};
}

OpResult BuggyManager::reset()
{
    // B1 (bug): a "fast reset" reopens without first tearing down the current
    // (possibly failed or removed) device. The old pointer is overwritten in
    // open() and its allocation is lost. A correct manager always closes first.
    // close();
    OpResult r = open();
    if (r.ok)
        ++recovered_;
    return r;
}

bool BuggyManager::is_usable() const
{
    // B2: reports the cached flag without consulting the real device, so it
    // claims "usable" after a hot-unplug even though the hardware is gone.
    return believe_open_;
}

} // namespace fitest
