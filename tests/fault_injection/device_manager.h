#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include "faulty_device.h"

#include <string>
#include <vector>

namespace fitest {

// High-level operations the test driver can request.
enum OpCode {
    OP_OPEN = 0,
    OP_CLOSE,
    OP_PLAY,
    OP_TICK,
    OP_HOTPLUG,
    OP_RESET,
    OP_CODE_COUNT
};

const char *op_name(OpCode op);

struct Op {
    OpCode code;
    FaultKind fault; // fault to arm before the op, or FAULT_KIND_COUNT for none
    FaultKind label; // fault kind this op represents (for reporting), or none
    unsigned int arg;
};

// Outcome of a single operation, used by the driver to assert invariants.
struct OpResult {
    bool ok;             // operation achieved its nominal goal
    bool recovered;      // device ended (or remained) in a usable/known state
    const char *reason;  // human readable note when !ok
};

// Common interface so the robust and deliberately-buggy managers can be
// driven by the identical 200-step scenario.
class IDeviceManager {
public:
    virtual ~IDeviceManager() = default;

    virtual OpResult open() = 0;
    virtual OpResult close() = 0;
    virtual OpResult play(unsigned int frames) = 0;
    virtual OpResult tick(unsigned int want) = 0;
    virtual OpResult hotplug() = 0;
    virtual OpResult reset() = 0;

    virtual bool is_usable() const = 0;
    virtual const char *name() const = 0;

    // Test hook: arm a fault on the currently owned device. Returns false if
    // there is no live device to receive it (the injector then keeps the fault
    // for the next created device instead).
    virtual bool inject_live_fault(FaultKind kind) = 0;

    // Counters describing how the manager reacted to faults.
    virtual unsigned int recovered_count() const = 0;
    virtual unsigned int swallowed_failures() const = 0;
};

} // namespace fitest

#endif
