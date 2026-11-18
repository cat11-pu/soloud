#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include "device_manager.h"

#include <functional>
#include <string>
#include <vector>

namespace fitest {

struct RoundReport {
    unsigned int rounds;
    unsigned int faults_per_kind[FAULT_KIND_COUNT];
    unsigned int errors_seen;
    unsigned int recoveries;
    unsigned int swallowed;       // fault results hidden from the caller
    bool leak_free;
    bool always_recoverable;
    long live_allocs_at_end;
    long peak_live_allocs;        // maximum simultaneously-live allocations
    long leaked_across_rounds;    // growth in live allocs at recovery boundary
    long total_allocs;
    long total_frees;
    size_t failure_op_index;      // op that first violated an invariant
    std::string failure; // non-empty iff an invariant was violated
    std::string leak_detail;
    std::string stale_detail;
    std::string swallow_detail;
};

using ManagerFactory = std::function<IDeviceManager *()>;

// Builds the deterministic 200-round scenario: fault kinds alternate, each
// fault is paired with the operation that triggers it, and benign
// play/tick/close operations are interleaved so recovery is always exercised.
std::vector<Op> build_scenario(unsigned int rounds, unsigned int seed);

// Runs a scenario against a freshly constructed manager and checks the three
// hard invariants: no crash (exceptions are caught), no leak (allocation
// accounting), and recoverable state (every fault is followed by a successful
// reset/open before the next fault).
RoundReport run_scenario(const std::vector<Op> &scenario,
                         ManagerFactory make_manager);

// ddmin-style reducer: returns the shortest subsequence that still fails.
std::vector<Op> minimize(const std::vector<Op> &failing,
                         ManagerFactory make_manager);

std::string render_sequence(const std::vector<Op> &seq);

// Focused, short scenarios that each isolate one concrete defect class.
// Used to show one minimal counterexample per bug, not just the first hit.
std::vector<Op> leak_scenario();       // reopen after a fault without close
std::vector<Op> stale_state_scenario(); // hot-unplug then query usability
std::vector<Op> swallow_scenario();    // callback timeout reported as success

} // namespace fitest

#endif
