#ifndef FAULTY_DEVICE_H
#define FAULTY_DEVICE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fitest {

// Kinds of fault the (simulated) hardware/back-end can be told to produce.
enum FaultKind {
    FAULT_OPEN_FAIL = 0,  // open() must fail before the device is usable
    FAULT_CB_TIMEOUT,     // audio callback exceeds its deadline once
    FAULT_UNDERRUN,       // consumer asks for more frames than were queued
    FAULT_OVERRUN,        // producer queues more frames than capacity allows
    FAULT_HOTPLUG,        // device disappears and later reappears
    FAULT_KIND_COUNT
};

const char *fault_name(FaultKind k);

enum DevState {
    DEV_CLOSED = 0,
    DEV_OPEN,
    DEV_FAILED
};

// A deterministic, in-process model of an audio device with a fixed-size
// ring buffer. Every failure path is exercised in software, so the test never
// touches real hardware and is fully reproducible from a seed.
class FaultyDevice {
public:
    FaultyDevice();
    ~FaultyDevice();

    FaultyDevice(const FaultyDevice &) = delete;
    FaultyDevice &operator=(const FaultyDevice &) = delete;

    // Lifecycle.
    bool open(unsigned int sample_rate, unsigned int channels,
              unsigned int capacity_frames);
    void close();
    bool is_open() const { return state_ == DEV_OPEN; }
    DevState state() const { return state_; }

    // Fault arming: the *next* matching operation triggers the fault.
    void arm(FaultKind kind);
    bool armed(FaultKind kind) const;

    // Producer side: queue frames. Returns frames actually accepted; on an
    // overrun the excess is dropped and the overrun counter increments.
    unsigned int queue_frames(unsigned int frame_count);

    // Consumer side: the "hardware" callback drains `want` frames. A timeout
    // fault makes the callback miss its deadline; an underrun drains whatever
    // is available and reports the shortfall.
    struct CallbackResult {
        unsigned int delivered;   // frames handed to the device
        unsigned int shortfall;   // frames missing (underrun)
        bool timed_out;           // deadline missed
    };
    CallbackResult callback(unsigned int want);

    // Hot-plug: physical removal. The device becomes unusable until close().
    void unplug();

    // Observed counters for invariant checking.
    unsigned int underruns() const { return underruns_; }
    unsigned int overruns() const { return overruns_; }
    unsigned int timeouts() const { return timeouts_; }
    unsigned int queued() const { return queued_; }
    unsigned int capacity() const { return capacity_; }

    // Process-wide allocation bookkeeping, used to prove "zero leak".
    static long live_allocations();
    static long total_allocations();
    static long total_frees();
    // Test-only: zero the process-wide counters. No device may be live when
    // this is called (each scenario fully tears its managers down).
    static void reset_counters();

private:
    void disarm(FaultKind kind);

    DevState state_;
    unsigned int capacity_;
    unsigned int queued_;
    unsigned int underruns_;
    unsigned int overruns_;
    unsigned int timeouts_;
    bool armed_[FAULT_KIND_COUNT];

    // A real allocation so ASan/LSan and our own counters can track it.
    float *ring_;

    static long s_live;
    static long s_total_alloc;
    static long s_total_free;
};

} // namespace fitest

#endif
