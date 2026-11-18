#ifndef BUGGY_MANAGER_H
#define BUGGY_MANAGER_H

#include "device_manager.h"
#include "fault_injector.h"

namespace fitest {

// Deliberately defective manager used as a counterexample. It contains three
// realistic bugs that the identical 200-step scenario must detect:
//   B1 leak:       open() after a failed/removed device never releases the
//                  old allocation, so live allocations grow without bound.
//   B2 stuck state: a hot-unplug leaves the manager believing the device is
//                  open; later play()/tick() fail and open() is refused, so
//                  the state is not recoverable without a reset.
//   B3 swallow:    callback timeouts are hidden from the caller (reported as
//                  success), so the stream keeps running on a dead callback.
class BuggyManager : public IDeviceManager {
public:
    BuggyManager(unsigned int sample_rate = 48000,
                 unsigned int channels = 2,
                 unsigned int capacity_frames = 256);
    ~BuggyManager() override;

    OpResult open() override;
    OpResult close() override;
    OpResult play(unsigned int frames) override;
    OpResult tick(unsigned int want) override;
    OpResult hotplug() override;
    OpResult reset() override;

    bool is_usable() const override;
    const char *name() const override { return "buggy"; }
    bool inject_live_fault(FaultKind kind) override
    {
        if (!dev_ || !dev_->is_open())
            return false;
        dev_->arm(kind);
        return true;
    }

    unsigned int recovered_count() const override { return recovered_; }
    unsigned int swallowed_failures() const override { return swallowed_; }

private:
    FaultyDevice *dev_; // B1: heap owned but sometimes abandoned
    unsigned int sample_rate_;
    unsigned int channels_;
    unsigned int capacity_;
    unsigned int recovered_;
    unsigned int swallowed_;
    bool believe_open_; // B2: cached flag not updated on unplug
};

} // namespace fitest

#endif
