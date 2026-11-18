#include "faulty_device.h"

#include <cstdlib>
#include <new>

namespace fitest {

long FaultyDevice::s_live = 0;
long FaultyDevice::s_total_alloc = 0;
long FaultyDevice::s_total_free = 0;

const char *fault_name(FaultKind k)
{
    switch (k) {
    case FAULT_OPEN_FAIL: return "OPEN_FAIL";
    case FAULT_CB_TIMEOUT: return "CB_TIMEOUT";
    case FAULT_UNDERRUN:  return "UNDERRUN";
    case FAULT_OVERRUN:   return "OVERRUN";
    case FAULT_HOTPLUG:   return "HOTPLUG";
    default:              return "?";
    }
}

FaultyDevice::FaultyDevice()
    : state_(DEV_CLOSED),
      capacity_(0),
      queued_(0),
      underruns_(0),
      overruns_(0),
      timeouts_(0),
      ring_(nullptr)
{
    for (int i = 0; i < FAULT_KIND_COUNT; ++i)
        armed_[i] = false;
}

FaultyDevice::~FaultyDevice()
{
    // A correct owner always close()s first, but never leak on destruction.
    close();
}

void FaultyDevice::arm(FaultKind kind)
{
    armed_[kind] = true;
}

bool FaultyDevice::armed(FaultKind kind) const
{
    return armed_[kind];
}

void FaultyDevice::disarm(FaultKind kind)
{
    armed_[kind] = false;
}

bool FaultyDevice::open(unsigned int sample_rate, unsigned int channels,
                        unsigned int capacity_frames)
{
    if (state_ != DEV_CLOSED)
        return false; // already open: a well-behaved caller treats as error

    if (armed(FAULT_OPEN_FAIL)) {
        disarm(FAULT_OPEN_FAIL);
        state_ = DEV_FAILED;
        return false;
    }

    if (sample_rate == 0 || channels == 0 || capacity_frames == 0) {
        state_ = DEV_FAILED;
        return false;
    }

    ring_ = new (std::nothrow) float[capacity_frames * channels]();
    if (!ring_) {
        state_ = DEV_FAILED;
        return false;
    }
    ++s_live;
    ++s_total_alloc;

    capacity_ = capacity_frames;
    queued_ = 0;
    underruns_ = overruns_ = timeouts_ = 0;
    state_ = DEV_OPEN;
    return true;
}

void FaultyDevice::close()
{
    if (ring_) {
        delete[] ring_;
        ring_ = nullptr;
        --s_live;
        ++s_total_free;
    }
    capacity_ = queued_ = 0;
    for (int i = 0; i < FAULT_KIND_COUNT; ++i)
        armed_[i] = false;
    state_ = DEV_CLOSED;
}

unsigned int FaultyDevice::queue_frames(unsigned int frame_count)
{
    if (state_ != DEV_OPEN)
        return 0;

    if (armed(FAULT_OVERRUN)) {
        disarm(FAULT_OVERRUN);
        // Force the request beyond capacity so the overflow path is taken.
        frame_count = capacity_ + frame_count + 1;
    }

    unsigned int room = (capacity_ > queued_) ? capacity_ - queued_ : 0;
    unsigned int accepted = frame_count < room ? frame_count : room;
    unsigned int dropped = frame_count - accepted;
    queued_ += accepted;
    if (dropped > 0)
        ++overruns_;
    return accepted;
}

FaultyDevice::CallbackResult FaultyDevice::callback(unsigned int want)
{
    CallbackResult res{0, 0, false};
    if (state_ != DEV_OPEN) {
        // Device gone (hot-unplug): nothing can be delivered.
        res.shortfall = want;
        ++underruns_;
        return res;
    }

    if (armed(FAULT_CB_TIMEOUT)) {
        disarm(FAULT_CB_TIMEOUT);
        res.timed_out = true;
        ++timeouts_;
        // A timed-out callback still consumes no valid frames this tick.
        return res;
    }

    if (armed(FAULT_UNDERRUN)) {
        disarm(FAULT_UNDERRUN);
        want = queued_ + want + 1; // guarantee a shortfall
    }

    res.delivered = want < queued_ ? want : queued_;
    res.shortfall = want - res.delivered;
    queued_ -= res.delivered;
    if (res.shortfall > 0)
        ++underruns_;
    return res;
}

void FaultyDevice::unplug()
{
    if (armed(FAULT_HOTPLUG))
        disarm(FAULT_HOTPLUG);
    if (state_ == DEV_OPEN) {
        // Pending buffered data is lost on physical removal.
        queued_ = 0;
        state_ = DEV_FAILED;
    }
}

long FaultyDevice::live_allocations()     { return s_live; }
long FaultyDevice::total_allocations()    { return s_total_alloc; }
long FaultyDevice::total_frees()          { return s_total_free; }

void FaultyDevice::reset_counters()
{
    s_live = 0;
    s_total_alloc = 0;
    s_total_free = 0;
}

} // namespace fitest
