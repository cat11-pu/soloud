#include "fault_injector.h"

namespace fitest {

FaultInjector &FaultInjector::instance()
{
    static FaultInjector s_inst;
    return s_inst;
}

void FaultInjector::arm(FaultKind kind)
{
    armed_ = kind;
}

FaultKind FaultInjector::consume()
{
    FaultKind k = armed_;
    armed_ = FAULT_KIND_COUNT;
    return k;
}

FaultKind FaultInjector::peek() const
{
    return armed_;
}

void FaultInjector::clear()
{
    armed_ = FAULT_KIND_COUNT;
}

FaultyDevice *make_device(unsigned int sample_rate, unsigned int channels,
                          unsigned int capacity_frames)
{
    // Open-fail is consumed at creation time; any other armed fault is handed
    // to the new device so its next matching operation triggers it.
    FaultKind pending = FaultInjector::instance().peek();

    FaultyDevice *dev = new FaultyDevice();
    if (pending == FAULT_OPEN_FAIL) {
        FaultInjector::instance().consume();
        dev->arm(FAULT_OPEN_FAIL);
    } else if (pending != FAULT_KIND_COUNT) {
        FaultInjector::instance().consume();
        dev->arm(pending);
    }

    if (!dev->open(sample_rate, channels, capacity_frames)) {
        // open() failed (the injected OPEN_FAIL or bad params). The manager
        // still owns the returned object and is responsible for deleting it.
        return dev;
    }
    return dev;
}

} // namespace fitest
