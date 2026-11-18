#include "buggy_manager.h"
#include "fault_injector.h"
#include "robust_manager.h"
#include "test_harness.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace fitest;

namespace {

constexpr unsigned int kRounds = 200;
constexpr unsigned int kSeed = 0xC0FFEEu;

void print_report(const char *label, const RoundReport &r)
{
    std::printf("================ %s ================\n", label);
    std::printf("faults injected : %u\n",
                r.faults_per_kind[0] + r.faults_per_kind[1] +
                r.faults_per_kind[2] + r.faults_per_kind[3] +
                r.faults_per_kind[4]);
    for (int k = 0; k < FAULT_KIND_COUNT; ++k)
        std::printf("  %-12s : %u\n", fault_name(static_cast<FaultKind>(k)),
                    r.faults_per_kind[k]);
    std::printf("error results   : %u\n", r.errors_seen);
    std::printf("recoveries      : %u\n", r.recoveries);
    std::printf("swallowed faults: %u\n", r.swallowed);
    std::printf("alloc/free      : %ld alloc, %ld free\n",
                r.total_allocs, r.total_frees);
    std::printf("live at end     : %ld (peak simultaneous: %ld)\n",
                r.live_allocs_at_end, r.peak_live_allocs);
    std::printf("zero leak       : %s\n", r.leak_free ? "PASS" : "FAIL");
    std::printf("recoverable     : %s\n",
                r.always_recoverable ? "PASS" : "FAIL");
    if (!r.failure.empty())
        std::printf("first violation : %s\n", r.failure.c_str());
    std::printf("\n");
}

int evaluate(const char *label, ManagerFactory factory,
             const std::vector<Op> &scenario, bool shrink)
{
    FaultInjector::instance().clear();
    RoundReport r = run_scenario(scenario, factory);
    print_report(label, r);

    if (r.failure.empty()) {
        std::printf("[%s] RESULT: PASS (zero crash, zero leak, recoverable)\n\n",
                    label);
        return 0;
    }

    std::printf("[%s] RESULT: FAIL\n", label);
    if (shrink) {
        std::printf("shrinking failing sequence (ddmin)...\n");
        std::vector<Op> minimal = minimize(scenario, factory);
        std::printf("minimal failing sequence (%zu ops):\n%s\n",
                    minimal.size(), render_sequence(minimal).c_str());

        // Re-run the minimal case to show exactly which invariant breaks.
        RoundReport mini = run_scenario(minimal, factory);
        std::printf("minimal-case violation: %s\n", mini.failure.c_str());
        std::printf("minimal-case live allocs: %ld (alloc %ld / free %ld)\n\n",
                    mini.live_allocs_at_end, mini.total_allocs,
                    mini.total_frees);
    }
    return 1;
}

void run_counterexamples(ManagerFactory robust, ManagerFactory buggy)
{
    struct Case {
        const char *title;
        std::vector<Op> (*build)();
    };
    Case cases[] = {
        {"B1 leak: reopen without releasing failed device", leak_scenario},
        {"B2 stale state: usable() lies after hot-unplug",  stale_state_scenario},
        {"B3 swallow: callback timeout reported as success", swallow_scenario},
    };

    std::printf("################ COUNTEREXAMPLES ################\n");
    for (const Case &c : cases) {
        std::vector<Op> seq = c.build();
        RoundReport rb = run_scenario(seq, robust);
        RoundReport bg = run_scenario(seq, buggy);

        std::printf("\n-- %s --\n", c.title);
        std::printf("sequence (%zu ops):\n%s", seq.size(),
                    render_sequence(seq).c_str());
        std::printf("  robust : %s (live=%ld, swallowed=%u)\n",
                    rb.failure.empty() ? "holds" : "FAILS",
                    rb.live_allocs_at_end, rb.swallowed);
        std::printf("  buggy  : %s (live=%ld, swallowed=%u)\n",
                    bg.failure.empty() ? "holds" : "FAILS",
                    bg.live_allocs_at_end, bg.swallowed);
        if (!bg.leak_detail.empty())
            std::printf("    . leak   : %s\n", bg.leak_detail.c_str());
        if (!bg.stale_detail.empty())
            std::printf("    . stale  : %s\n", bg.stale_detail.c_str());
        if (!bg.swallow_detail.empty())
            std::printf("    . swallow: %s\n", bg.swallow_detail.c_str());
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char **argv)
{
    bool shrink = true;
    bool run_buggy = true;
    unsigned int seed = kSeed;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-shrink") shrink = false;
        if (a == "--robust-only") run_buggy = false;
        if (a.rfind("--seed=", 0) == 0)
            seed = static_cast<unsigned int>(
                std::strtoul(a.c_str() + 7, nullptr, 0));
    }

    std::vector<Op> scenario = build_scenario(kRounds, seed);
    std::printf("SoLoud fault-injection harness: %u rounds, %zu ops, seed 0x%X\n\n",
                kRounds, scenario.size(), seed);

    auto robust_factory = []() -> IDeviceManager * {
        return new RobustManager(48000, 2, 256);
    };
    auto buggy_factory = []() -> IDeviceManager * {
        return new BuggyManager(48000, 2, 256);
    };

    int rc = 0;
    rc |= evaluate("robust manager (code under test)", robust_factory,
                   scenario, false);
    if (run_buggy)
    {
        rc |= evaluate("buggy manager (counterexample)", buggy_factory,
                       scenario, shrink);
        run_counterexamples(robust_factory, buggy_factory);
    }

    std::printf("harness exit code: %d%s\n", rc,
                rc ? " (counterexample demonstrated)" : " (all invariants held)");
    return rc;
}
