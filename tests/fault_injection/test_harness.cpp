#include "test_harness.h"

#include "fault_injector.h"

#include <cstdio>
#include <exception>
#include <sstream>

namespace fitest {

const char *op_name(OpCode op)
{
    switch (op) {
    case OP_OPEN:    return "OPEN";
    case OP_CLOSE:   return "CLOSE";
    case OP_PLAY:    return "PLAY";
    case OP_TICK:    return "TICK";
    case OP_HOTPLUG: return "HOTPLUG";
    case OP_RESET:   return "RESET";
    default:         return "?";
    }
}

namespace {

// Small deterministic PRNG (xorshift) so a run is fully reproducible.
struct Prng {
    uint32_t state;
    explicit Prng(uint32_t seed) : state(seed ? seed : 1u) {}
    uint32_t next()
    {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

Op op_for_fault(FaultKind fault, Prng &rng)
{
    switch (fault) {
    case FAULT_OPEN_FAIL:
        // OPEN_FAIL is armed globally and consumed when the manager creates
        // its next device via a RESET/OPEN.
        return Op{OP_RESET, FAULT_OPEN_FAIL, FAULT_OPEN_FAIL, 0};
    case FAULT_CB_TIMEOUT:
        return Op{OP_TICK, FAULT_CB_TIMEOUT, FAULT_CB_TIMEOUT,
                  64 + rng.below(128)};
    case FAULT_UNDERRUN:
        return Op{OP_TICK, FAULT_UNDERRUN, FAULT_UNDERRUN,
                  64 + rng.below(128)};
    case FAULT_OVERRUN:
        return Op{OP_PLAY, FAULT_OVERRUN, FAULT_OVERRUN, 32 + rng.below(64)};
    case FAULT_HOTPLUG:
        // The HOTPLUG op itself performs the removal; no device arm needed.
        return Op{OP_HOTPLUG, FAULT_KIND_COUNT, FAULT_HOTPLUG, 0};
    default:
        return Op{OP_TICK, FAULT_KIND_COUNT, FAULT_KIND_COUNT, 64};
    }
}

// Executes one op and returns false if the op itself triggers a hard failure
// (exception or an invariant violation handled by the caller via the report).
OpResult dispatch(IDeviceManager &mgr, const Op &op)
{
    switch (op.code) {
    case OP_OPEN:
        return mgr.open();
    case OP_CLOSE:   return mgr.close();
    case OP_PLAY:    return mgr.play(op.arg ? op.arg : 64);
    case OP_TICK:    return mgr.tick(op.arg ? op.arg : 64);
    case OP_HOTPLUG: return mgr.hotplug();
    case OP_RESET:   return mgr.reset();
    default:         return OpResult{false, false, "bad opcode"};
    }
}

} // namespace

std::vector<Op> build_scenario(unsigned int rounds, unsigned int seed)
{
    std::vector<Op> seq;
    Prng rng(seed);
    seq.reserve(rounds * 4);

    for (unsigned int r = 0; r < rounds; ++r) {
        // Strict alternation across the five fault kinds, as requested.
        FaultKind fault = static_cast<FaultKind>(r % FAULT_KIND_COUNT);

        // 1) Clean recovery boundary: guarantee a usable device first.
        seq.push_back(Op{OP_RESET, FAULT_KIND_COUNT, FAULT_KIND_COUNT, 0});

        // 2) Inject the fault with the operation that triggers it.
        Op trigger = op_for_fault(fault, rng);
        seq.push_back(trigger);

        // 3) Benign interleaving so continued operation is exercised; these
        // run against whatever state the fault left behind.
        unsigned int interleave = rng.below(3);
        for (unsigned int i = 0; i < interleave; ++i) {
            OpCode benign = (rng.below(2) == 0) ? OP_PLAY : OP_TICK;
            seq.push_back(Op{benign, FAULT_KIND_COUNT, FAULT_KIND_COUNT,
                             32 + rng.below(96)});
        }

        // 4) Explicit recovery boundary: every fault must be recoverable.
        seq.push_back(Op{OP_RESET, FAULT_KIND_COUNT, FAULT_KIND_COUNT, 0});
        seq.push_back(Op{OP_CLOSE, FAULT_KIND_COUNT, FAULT_KIND_COUNT, 0});
    }
    return seq;
}

RoundReport run_scenario(const std::vector<Op> &scenario,
                         ManagerFactory make_manager)
{
    RoundReport rep;
    // Each run is an isolated accounting context; the counters are process
    // global but must start at zero so repeated ddmin runs stay reproducible.
    FaultyDevice::reset_counters();
    FaultInjector::instance().clear();
    rep.rounds = 0;
    for (int k = 0; k < FAULT_KIND_COUNT; ++k) rep.faults_per_kind[k] = 0;
    rep.errors_seen = 0;
    rep.recoveries = 0;
    rep.swallowed = 0;
    rep.leak_free = true;
    rep.always_recoverable = true;
    rep.live_allocs_at_end = 0;
    rep.peak_live_allocs = 0;
    rep.leaked_across_rounds = 0;
    rep.total_allocs = 0;
    rep.total_frees = 0;
    rep.failure_op_index = static_cast<size_t>(-1);
    rep.leak_detail.clear();
    rep.stale_detail.clear();
    rep.swallow_detail.clear();

    long allocs_before = FaultyDevice::total_allocations();
    long frees_before = FaultyDevice::total_frees();

    IDeviceManager *mgr = nullptr;
    // At most one device may be live at any instant. Two simultaneous
    // live allocations prove an open() abandoned a previous device.
    long peak_live = 0;
    // Live allocations at the equivalent point of the previous round. The
    // scenario opens exactly one device per round and closes it at the end, so
    // live count at matching points must never grow. Growth means a device was
    // abandoned (a leak the end-of-run destructor would otherwise hide).
    long live_at_round_start = FaultyDevice::live_allocations();
    bool round_start_captured = false;
    bool have_baseline = false;

    try {
        mgr = make_manager();

        bool awaiting_recovery = false;
        FaultKind pending = FAULT_KIND_COUNT;
        unsigned int fault_index = 0;

        auto fail = [&](size_t op_index, const std::string &msg) {
            if (rep.failure.empty()) {
                rep.failure = msg;
                rep.failure_op_index = op_index;
            }
        };

        for (size_t i = 0; i < scenario.size(); ++i) {
            const Op &op = scenario[i];

            // Arm the fault before the operation. OPEN_FAIL is consumed at
            // device creation; runtime faults are pushed onto the live device.
            if (op.fault != FAULT_KIND_COUNT) {
                if (op.fault == FAULT_OPEN_FAIL) {
                    // Must survive until make_device() runs inside RESET.
                    FaultInjector::instance().arm(FAULT_OPEN_FAIL);
                } else {
                    bool delivered = mgr->inject_live_fault(op.fault);
                    if (!delivered)
                        FaultInjector::instance().arm(op.fault);
                }
                pending = op.label;
                fault_index = static_cast<unsigned int>(i);
                awaiting_recovery = true;
            }
            if (op.label != FAULT_KIND_COUNT) {
                ++rep.faults_per_kind[op.label];
            }

            OpResult res = dispatch(*mgr, op);

            // Defensive: never leave an armed creation-time fault for a later,
            // unrelated operation.
            FaultInjector::instance().clear();

            if (!res.ok)
                ++rep.errors_seen;

            // Invariant 1 (zero leak): at every step the number of live
            // device allocations must be 0 (closed) or 1 (one open device).
            long live = FaultyDevice::live_allocations();
            if (live > peak_live)
                peak_live = live;
            if (live > 1) {
                rep.leak_free = false;
                std::ostringstream os;
                os << "leak at op " << i << " (" << op_name(op.code)
                   << "): " << live << " devices live (expected 0 or 1) after "
                   << fault_name(pending);
                if (rep.leak_detail.empty())
                    rep.leak_detail = os.str();
                fail(i, os.str());
            }

            // Invariant 1b: the first clean RESET of each round is the same
            // program point. A higher live count here than at the previous
            // round's equivalent point proves a device leaked across rounds.
            if (op.fault == FAULT_KIND_COUNT && op.code == OP_RESET &&
                !round_start_captured) {
                long boundary_live = FaultyDevice::live_allocations();
                if (have_baseline && boundary_live > live_at_round_start) {
                    rep.leak_free = false;
                    rep.leaked_across_rounds +=
                        boundary_live - live_at_round_start;
                    std::ostringstream os;
                    os << "leak across recovery at op " << i
                       << " (round-start RESET): live grew "
                       << live_at_round_start
                       << " -> " << boundary_live
                       << " after " << fault_name(pending);
                    if (rep.leak_detail.empty())
                        rep.leak_detail = os.str();
                    fail(i, os.str());
                }
                live_at_round_start = boundary_live;
                round_start_captured = true;
                have_baseline = true;
            }
            if (op.code == OP_CLOSE && op.fault == FAULT_KIND_COUNT &&
                i + 1 < scenario.size()) {
                // Next op begins a new round's recovery boundary.
                round_start_captured = false;
            }

            // Invariant 2 (no swallowed fault): a fault-injecting op must
            // surface the failure rather than report success. Timeout and
            // hotplug are hard faults; open-fail must fail; underrun/overrun
            // are soft and may return ok with an explanatory reason.
            if (op.label != FAULT_KIND_COUNT &&
                (op.label == FAULT_CB_TIMEOUT ||
                 op.label == FAULT_HOTPLUG ||
                 op.label == FAULT_OPEN_FAIL)) {
                if (res.ok) {
                    ++rep.swallowed;
                    std::ostringstream os2;
                    os2 << "swallowed " << fault_name(op.label)
                       << " at op " << i << " (" << op_name(op.code)
                       << "): reported success on a hard fault";
                    if (rep.swallow_detail.empty())
                        rep.swallow_detail = os2.str();
                    fail(i, os2.str());
                }
            }

            // Invariant 3 (recoverable): once a fault has been injected, the
            // first clean RESET must restore a usable device.
            // Recovery invariant: the first clean RESET after an injected
            // fault must bring the manager back to a usable state.
            if (awaiting_recovery && op.fault == FAULT_KIND_COUNT &&
                op.code == OP_RESET) {
                if (!res.ok || !mgr->is_usable()) {
                    rep.always_recoverable = false;
                    std::ostringstream os;
                    os << "not recoverable after " << fault_name(pending)
                       << " (armed at op " << fault_index
                       << "): reset ok=" << res.ok
                       << " usable=" << mgr->is_usable();
                    fail(i, os.str());
                } else {
                    ++rep.recoveries;
                }
                awaiting_recovery = false;
                pending = FAULT_KIND_COUNT;
            }

            // Invariant 3b: immediately after a hard fault that invalidates
            // the device (hotplug, timeout), the manager must stop claiming
            // the device is usable. Lying here means state cannot be trusted
            // and recovery is not guaranteed.
            if (op.label == FAULT_HOTPLUG || op.label == FAULT_CB_TIMEOUT) {
                if (mgr->is_usable()) {
                    rep.always_recoverable = false;
                    std::ostringstream os;
                    os << "stale state after " << fault_name(op.label)
                       << " at op " << i << " (" << op_name(op.code)
                       << "): manager still claims usable after hard fault";
                    if (rep.stale_detail.empty())
                        rep.stale_detail = os.str();
                    fail(i, os.str());
                }
            }
        }

        delete mgr;
        mgr = nullptr;
    } catch (const std::exception &e) {
        rep.failure = std::string("crash/exception: ") + e.what();
        rep.failure_op_index = static_cast<size_t>(-1);
        delete mgr;
    } catch (...) {
        rep.failure = "crash/unknown exception";
        rep.failure_op_index = static_cast<size_t>(-1);
        delete mgr;
    }

    rep.total_allocs = FaultyDevice::total_allocations() - allocs_before;
    rep.total_frees = FaultyDevice::total_frees() - frees_before;
    rep.live_allocs_at_end = FaultyDevice::live_allocations();
    rep.peak_live_allocs = peak_live;
    if (rep.live_allocs_at_end != 0)
        rep.leak_free = false;
    if (rep.total_allocs != rep.total_frees)
        rep.leak_free = false;

    if (!rep.leak_free && rep.failure.empty()) {
        std::ostringstream os;
        os << "leak: " << rep.total_allocs << " alloc vs "
           << rep.total_frees << " free, "
           << rep.live_allocs_at_end << " still live";
        rep.failure = os.str();
    }
    return rep;
}

namespace {

bool fails(const std::vector<Op> &seq, ManagerFactory make_manager)
{
    RoundReport r = run_scenario(seq, make_manager);
    return !r.failure.empty();
}

} // namespace

std::vector<Op> minimize(const std::vector<Op> &failing,
                         ManagerFactory make_manager)
{
    std::vector<Op> cur = failing;
    size_t granularity = 2;

    while (granularity <= cur.size()) {
        size_t chunks = (cur.size() + granularity - 1) / granularity;
        bool reduced = false;
        for (size_t c = 0; c < chunks; ++c) {
            size_t begin = c * granularity;
            size_t end = begin + granularity;
            if (end > cur.size()) end = cur.size();

            std::vector<Op> candidate;
            candidate.reserve(cur.size() - (end - begin));
            for (size_t i = 0; i < cur.size(); ++i)
                if (i < begin || i >= end)
                    candidate.push_back(cur[i]);

            if (!candidate.empty() && fails(candidate, make_manager)) {
                cur.swap(candidate);
                reduced = true;
                break;
            }
        }
        if (!reduced)
            granularity *= 2;
    }
    return cur;
}

std::string render_sequence(const std::vector<Op> &seq)
{
    std::ostringstream os;
    for (size_t i = 0; i < seq.size(); ++i) {
        const Op &op = seq[i];
        os << "  [" << i << "] " << op_name(op.code);
        if (op.code == OP_PLAY || op.code == OP_TICK)
            os << "(" << op.arg << ")";
        if (op.label != FAULT_KIND_COUNT)
            os << " +FAULT(" << fault_name(op.label) << ")";
        os << "\n";
    }
    return os.str();
}

std::vector<Op> leak_scenario()
{
    // Open a healthy device, hot-unplug it, then "fast reset" by reopening
    // without releasing the removed device. A correct manager frees the old
    // device first; the buggy one leaves it allocated and unreachable.
    return {
        Op{OP_RESET, FAULT_KIND_COUNT, FAULT_KIND_COUNT, 0},
        Op{OP_HOTPLUG, FAULT_KIND_COUNT, FAULT_HOTPLUG, 0},
        Op{OP_RESET, FAULT_KIND_COUNT, FAULT_KIND_COUNT, 0},
    };
}

std::vector<Op> stale_state_scenario()
{
    // After physical removal the manager must report the device as not usable.
    return {
        Op{OP_RESET, FAULT_KIND_COUNT, FAULT_KIND_COUNT, 0},
        Op{OP_HOTPLUG, FAULT_KIND_COUNT, FAULT_HOTPLUG, 0},
    };
}

std::vector<Op> swallow_scenario()
{
    // A callback that misses its deadline is a hard fault; the manager must
    // report failure and recover, not claim the tick succeeded.
    return {
        Op{OP_RESET, FAULT_KIND_COUNT, FAULT_KIND_COUNT, 0},
        Op{OP_TICK, FAULT_CB_TIMEOUT, FAULT_CB_TIMEOUT, 128},
    };
}

} // namespace fitest
