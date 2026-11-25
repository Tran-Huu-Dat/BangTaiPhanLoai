// Compile-time check (works where EXE is blocked):
//   g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only tests/test_station1_servo_trial.cpp
#include "../station1_servo_trial_logic.h"

namespace {
using E = TrialEvent;
using S = TrialStage;

// Runs the gate from t0 to t1 (1 ms steps) with LM393 active in [on, off). Counts events.
struct Run {
    int push = 0, passed = 0, timed_out = 0, unmatched = 0, ready = 0;
};
constexpr void run(Station1TrialGate& g, Run& r, std::uint32_t t0, std::uint32_t t1,
                   std::uint32_t on, std::uint32_t off) {
    for (std::uint32_t t = t0; t < t1; ++t) {
        switch (g.tick(t, true, true, t >= on && t < off)) {
        case E::Push: ++r.push; break;
        case E::Passed: ++r.passed; break;
        case E::TimedOut: ++r.timed_out; break;
        case E::Unmatched: ++r.unmatched; break;
        case E::Ready: ++r.ready; break;
        default: break;
        }
    }
}

// Fixed timings so the scenarios below don't change when the firmware defaults are retuned.
constexpr Station1TrialGate base() {
    Station1TrialGate g;
    g.position_stable_ms = 20;
    g.wait_limit_ms = 1500;
    g.push_ms = 250;
    g.recover_ms = 200;
    g.clear_ms = 60;
    return g;
}

constexpr bool l_pushes_once_with_set_timings() {
    Station1TrialGate g = base();
    Run r;
    g.enqueue(true, 0);
    run(g, r, 0, 119, 100, 400);
    if (r.push != 0) return false;  // LM393 active 19 ms < 20 ms
    if (g.tick(120, true, true, true) != E::Push || g.stage != S::Extended) return false;
    if (g.tick(369, true, true, true) != E::None) return false;
    if (g.tick(370, true, true, true) != E::Returned) return false;  // 250 ms hold
    if (g.tick(569, true, true, true) != E::None) return false;
    if (g.tick(570, true, true, true) != E::Ready || g.stage != S::Idle) return false;  // 200 ms retract
    run(g, r, 571, 2000, 100, 400);
    return r.push == 0 && r.unmatched == 0 && g.q_count == 0;
}

constexpr bool lm393_before_tof_decision_still_pushes() {
    Station1TrialGate g = base();
    Run r;
    run(g, r, 0, 50, 0, 300);  // item already at LM393, ToF not decided yet
    g.enqueue(true, 50);
    run(g, r, 50, 1000, 0, 300);
    return r.push == 1 && r.unmatched == 0;
}

constexpr bool s_m_pass_without_push() {
    Station1TrialGate g = base();
    Run r;
    g.enqueue(false, 0);
    run(g, r, 0, 1000, 100, 400);
    return r.push == 0 && r.passed == 1 && g.stage == S::Idle;
}

constexpr bool queue_keeps_order_l_m_l() {  // three items in flight before the first arrives
    Station1TrialGate g = base();
    Run r;
    g.enqueue(true, 0);
    g.enqueue(false, 10);
    g.enqueue(true, 20);
    run(g, r, 0, 700, 100, 400);
    if (r.push != 1 || r.passed != 0) return false;
    run(g, r, 700, 1100, 700, 900);
    if (r.passed != 1) return false;
    run(g, r, 1100, 2000, 1100, 1300);
    return r.push == 2 && r.timed_out == 0 && g.q_count == 0;
}

constexpr bool lm393_never_arrives_times_out() {
    Station1TrialGate g = base();
    Run r;
    g.enqueue(true, 0);
    run(g, r, 0, 1499, 9999, 9999);
    if (r.timed_out != 0) return false;
    run(g, r, 1499, 1600, 9999, 9999);
    return r.timed_out == 1 && r.push == 0 && g.q_count == 0;
}

constexpr bool lm393_noise_is_ignored() {
    Station1TrialGate g = base();
    Run r;
    g.enqueue(true, 0);
    run(g, r, 0, 100, 10, 29);   // 19 ms glitch
    run(g, r, 100, 200, 130, 149);
    return r.push == 0 && r.unmatched == 0 && g.q_count == 1;
}

constexpr bool item_without_tof_decision_is_unmatched() {
    Station1TrialGate g = base();
    Run r;
    run(g, r, 0, 500, 100, 300);
    return r.unmatched == 1 && r.push == 0;
}

constexpr bool stuck_active_lm393_consumes_only_one_item() {
    Station1TrialGate g = base();
    Run r;
    g.enqueue(true, 0);
    g.enqueue(true, 0);
    run(g, r, 0, 1400, 0, 5000);  // LM393 never clears
    return r.push == 1 && g.q_count == 1;
}

constexpr bool tof_loss_aborts_and_clears_queue() {
    Station1TrialGate g = base();
    g.enqueue(true, 0);
    if (g.tick(5, true, false, true) != E::SensorLost) return false;
    return g.q_count == 0 && g.tick(10, true, true, true) == E::None;
}

constexpr bool disable_during_push_returns_idle() {
    Station1TrialGate g = base();
    Run r;
    g.enqueue(true, 0);
    run(g, r, 0, 30, 0, 999);
    if (r.push != 1 || g.stage != S::Extended) return false;
    g.tick(50, false, true, true);  // pause / E-stop mid push
    return g.stage == S::Idle && g.q_count == 0;
}

constexpr bool queue_full_is_reported() {
    Station1TrialGate g = base();
    for (std::size_t i = 0; i < Station1TrialGate::QUEUE_MAX; ++i)
        if (!g.enqueue(true, 0)) return false;
    return !g.enqueue(true, 0);
}

constexpr bool manual_push_only_from_idle() {
    Station1TrialGate g = base();
    if (!g.force_push(0) || g.force_push(1)) return false;
    return g.tick(250, true, true, false) == E::Returned;
}

static_assert(l_pushes_once_with_set_timings());
static_assert(lm393_before_tof_decision_still_pushes());
static_assert(s_m_pass_without_push());
static_assert(queue_keeps_order_l_m_l());
static_assert(lm393_never_arrives_times_out());
static_assert(lm393_noise_is_ignored());
static_assert(item_without_tof_decision_is_unmatched());
static_assert(stuck_active_lm393_consumes_only_one_item());
static_assert(tof_loss_aborts_and_clears_queue());
static_assert(disable_during_push_returns_idle());
static_assert(queue_full_is_reported());
static_assert(manual_push_only_from_idle());
}  // namespace

int main() { return 0; }
