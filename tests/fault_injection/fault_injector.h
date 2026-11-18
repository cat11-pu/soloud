#ifndef FAULT_INJECTOR_H
#define FAULT_INJECTOR_H

#include "faulty_device.h"

namespace fitest {

// Process-wide fault injection point. The test driver arms a fault; the next
// device created via make_device() (open-fail) or the next matching device
// operation consumes it. Keeping it global lets the driver inject faults
// even though the managers own their devices.
class FaultInjector {
public:
    static FaultInjector &instance();

    // Arm a fault to be consumed by the next operation/device.
    void arm(FaultKind kind);
    // Take (and clear) the currently armed fault, if any.
    FaultKind consume();
    FaultKind peek() const;
    void clear();

private:
    FaultInjector() : armed_(FAULT_KIND_COUNT) {}
    FaultKind armed_;
};

// Factory every manager uses to obtain a device, so injection is unavoidable
// and both managers are exercised identically.
FaultyDevice *make_device(unsigned int sample_rate, unsigned int channels,
                          unsigned int capacity_frames);

} // namespace fitest

#endif
