#ifndef ROBUST_MANAGER_H
#define ROBUST_MANAGER_H

#include "device_manager.h"
#include "fault_injector.h"

namespace fitest {

// Robust policy:
//  - every device is fully released before it is reopened (no leak);
//  - every fault leaves the manager in CLOSED or a freshly OPEN state
//    (always recoverable);
//  - a failed operation is reported, never silently swallowed;
//  - hot-unplug triggers an orderly teardown so the next open() works.
class RobustManager : public IDeviceManager {
public:
    RobustManager(unsigned int sample_rate = 48000,
                  unsigned int channels = 2,
                  unsigned int capacity_frames = 256);
    ~RobustManager() override;

    OpResult open() override;
    OpResult close() override;
    OpResult play(unsigned int frames) override;
    OpResult tick(unsigned int want) override;
    OpResult hotplug() override;
    OpResult reset() override;

    bool is_usable() const override;
    const char *name() const override { return "robust"; }
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
    void teardown_locked();

    FaultyDevice *dev_;
    unsigned int sample_rate_;
    unsigned int channels_;
    unsigned int capacity_;
    unsigned int recovered_;
    unsigned int swallowed_;
};

} // namespace fitest

#endif
