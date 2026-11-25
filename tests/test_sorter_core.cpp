// Compile-time verification (no EXE needed, works under Windows Application Control):
//   g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only tests/test_sorter_core.cpp
// Full host binary (WSL/Linux/CI):
//   g++ -std=c++17 -Wall -Wextra -Werror tests/test_sorter_core.cpp -o test_sorter_core && ./test_sorter_core
#include "../sorter_core.h"

#include <cassert>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace {

using namespace sorter_core;

constexpr bool approx_eq(double a, double b, double eps = 1e-9) noexcept {
    const double diff = a - b;
    return (diff < 0.0 ? -diff : diff) <= eps;
}

constexpr bool test_classify_and_route() noexcept {
    // Boundary and invalid threshold checks (SORT-R04)
    if (classify(40.0, 40.0, 60.0) != Size::S) return false;
    if (classify(40.0 + 1e-12, 40.0, 60.0) != Size::M) return false;
    if (classify(60.0, 40.0, 60.0) != Size::M) return false;
    if (classify(60.0 + 1e-12, 40.0, 60.0) != Size::L) return false;
    if (classify(50.0, 60.0, 40.0) != Size::Invalid) return false;
    if (classify(50.0, 40.0, 40.0) != Size::Invalid) return false;

    const double invalids[3] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
    };
    for (double bad : invalids) {
        if (classify(bad, 40.0, 60.0) != Size::Invalid) return false;
        if (classify(50.0, bad, 60.0) != Size::Invalid) return false;
        if (classify(50.0, 40.0, bad) != Size::Invalid) return false;
    }

    // D-13 / SORT-R05 routing matrix (Tram 1 -> L, Tram 2 -> M, S -> Pass)
    if (route(Size::S, 1) != Action::Pass) return false;
    if (route(Size::S, 2) != Action::Pass) return false;
    if (route(Size::M, 1) != Action::Pass) return false;
    if (route(Size::M, 2) != Action::Push) return false;
    if (route(Size::L, 1) != Action::Push) return false;
    if (route(Size::L, 2) != Action::Missed) return false;
    if (route(Size::Invalid, 1) != Action::Invalid) return false;
    if (route(Size::Invalid, 2) != Action::Invalid) return false;
    if (route(Size::M, 0) != Action::Invalid) return false;
    if (route(Size::L, 3) != Action::Invalid) return false;

    // D-14 independent height ranges with guard band [18..24] -> S (pass through)
    if (classify_ranges(12.0, 7.0, 18.0, 24.0, 60.0) != Size::M) return false;
    if (classify_ranges(21.0, 7.0, 18.0, 24.0, 60.0) != Size::S) return false;
    if (classify_ranges(30.0, 7.0, 18.0, 24.0, 60.0) != Size::L) return false;
    if (classify_ranges(5.0, 7.0, 18.0, 24.0, 60.0) != Size::S) return false;
    if (classify_ranges(65.0, 7.0, 18.0, 24.0, 60.0) != Size::S) return false;
    // Overlap gives L priority
    if (classify_ranges(20.0, 7.0, 22.0, 18.0, 60.0) != Size::L) return false;
    // Invalid bounds rejected
    if (classify_ranges(15.0, 18.0, 18.0, 24.0, 60.0) != Size::Invalid) return false;
    if (classify_ranges(15.0, -1.0, 18.0, 24.0, 60.0) != Size::Invalid) return false;
    if (classify_ranges(15.0, 7.0, 18.0, 60.0, 24.0) != Size::Invalid) return false;
    return true;
}

constexpr bool test_height_and_median() noexcept {
    // SORT-R03 / C2 linear height calibration
    if (!approx_eq(calibrate_height(50.0, 100.0), 50.0)) return false;
    if (!approx_eq(calibrate_height(50.0, 100.0, 1.0, 0.0), 50.0)) return false;
    if (!approx_eq(calibrate_height(50.0, 100.0, 2.0, 10.0), 20.0)) return false;
    if (is_finite(calibrate_height(50.0, 100.0, 0.0, 0.0))) return false;
    if (is_finite(calibrate_height(50.0, 100.0, -1.0, 0.0))) return false;
    if (is_finite(calibrate_height(std::numeric_limits<double>::quiet_NaN(), 100.0))) return false;

    // Median odd/even + 1-point outlier rejection
    const double s3[3] = {50.0, 50.2, 49.9};
    if (!approx_eq(median_of(s3, 3), 50.0)) return false;

    const double s4[4] = {10.0, 40.0, 20.0, 30.0};
    if (!approx_eq(median_of(s4, 4), 25.0)) return false;

    const double s5_spike[5] = {50.0, 99.0, 50.2, 49.9, 50.1};
    if (!approx_eq(median_of(s5_spike, 5), 50.1)) return false;

    const double s_nan[3] = {50.0, std::numeric_limits<double>::quiet_NaN(), 49.9};
    if (is_finite(median_of(s_nan, 3))) return false;
    if (is_finite(median_of(s3, 0))) return false;
    if (is_finite(median_of(nullptr, 3))) return false;
    return true;
}

constexpr bool test_item_detector_sort_ac02() noexcept {
    // Exact SPEC-SORT §9 / SORT-AC02 vector: [99, 75, 50, 50.2, 49.9], H0=100, T_SM=40, T_ML=60
    StationConfig cfg1{};
    cfg1.station = 1;
    cfg1.h0_mm = 100.0;
    cfg1.detect_delta_mm = 5.0;
    cfg1.clear_delta_mm = 5.0;
    cfg1.threshold_sm_mm = 40.0;
    cfg1.threshold_ml_mm = 60.0;
    cfg1.n_samples = 3;
    cfg1.clear_samples = 2;
    cfg1.discard_first_edge_sample = true;

    ItemDetector st1(cfg1);
    if (st1.feed(99.0, 11960).ready) return false;   // belt
    if (st1.feed(75.0, 12000).ready) return false;   // edge sample discarded, t0=12000
    if (st1.feed(50.0, 12040).ready) return false;   // sample 1
    if (st1.feed(50.2, 12080).ready) return false;   // sample 2
    const DetectionResult r1 = st1.feed(49.9, 12120);  // sample 3 -> decision
    if (!r1.ready) return false;
    if (r1.t0_ms != 12000) return false;
    if (!approx_eq(r1.median_distance_mm, 50.0)) return false;
    if (!approx_eq(r1.height_mm, 50.0)) return false;
    if (r1.size != Size::M) return false;
    if (r1.action != Action::Pass) return false;

    // Anti-double-trigger & error samples must NOT unlock WaitClear (D-03)
    if (st1.state() != ItemDetector::State::WaitClear) return false;
    if (st1.feed(50.1, 12160).ready) return false;
    if (st1.feed(std::numeric_limits<double>::quiet_NaN(), 12200).ready) return false;
    if (st1.feed(0.0, 12240).ready) return false;
    if (st1.feed(4500.0, 12280).ready) return false;
    if (st1.state() != ItemDetector::State::WaitClear) return false;
    if (st1.feed(98.0, 12320).ready) return false;   // 1st clear sample
    if (st1.state() != ItemDetector::State::WaitClear) return false;
    if (st1.feed(99.0, 12360).ready) return false;   // 2nd clear sample -> re-armed
    if (st1.state() != ItemDetector::State::Idle) return false;

    // Same M vector reaches Station 2 -> Push at Station 2
    StationConfig cfg2 = cfg1;
    cfg2.station = 2;
    ItemDetector st2(cfg2);
    st2.feed(99.0, 100);
    st2.feed(75.0, 140);
    st2.feed(50.0, 180);
    st2.feed(50.2, 220);
    const DetectionResult r2_m = st2.feed(49.9, 260);
    if (!r2_m.ready || r2_m.size != Size::M || r2_m.action != Action::Push) {
        return false;
    }

    // Re-arm Station 2 and verify escaped L -> Missed at Station 2 (never Push)
    st2.feed(98.0, 300);
    st2.feed(98.5, 340);
    st2.feed(60.0, 400);  // edge
    st2.feed(30.0, 440);
    st2.feed(30.1, 480);
    const DetectionResult r2_l = st2.feed(29.9, 520);
    if (!r2_l.ready || !approx_eq(r2_l.height_mm, 70.0) ||
        r2_l.size != Size::L || r2_l.action != Action::Missed) {
        return false;
    }

    // Verify L pushes at Station 1 and S passes at Station 1 without pushing
    st1.feed(45.0, 15000);  // edge
    st1.feed(30.0, 15040);
    st1.feed(30.2, 15080);
    const DetectionResult r1_l = st1.feed(29.8, 15120);
    if (!r1_l.ready || !approx_eq(r1_l.height_mm, 70.0) ||
        r1_l.size != Size::L || r1_l.action != Action::Push) {
        return false;
    }
    st1.feed(99.0, 15160);
    st1.feed(99.0, 15200);

    st1.feed(85.0, 20000);  // edge
    st1.feed(70.0, 20040);
    st1.feed(70.2, 20080);
    const DetectionResult r1_s = st1.feed(69.9, 20120);
    if (!r1_s.ready || !approx_eq(r1_s.height_mm, 30.0) ||
        r1_s.size != Size::S || r1_s.action != Action::Pass) {
        return false;
    }

    // Glitch (single edge sample followed immediately by belt) -> resets to Idle without firing
    st1.feed(99.0, 20160);
    st1.feed(99.0, 20200);
    if (st1.state() != ItemDetector::State::Idle) return false;
    if (st1.feed(75.0, 20300).ready) return false;
    if (st1.feed(99.0, 20340).ready) return false;
    if (st1.state() != ItemDetector::State::Idle) return false;

    // Dynamic set_config rejects invalid thresholds and applies valid new thresholds.
    StationConfig bad_cfg = cfg1;
    bad_cfg.threshold_ml_mm = bad_cfg.threshold_sm_mm;
    if (st1.set_config(bad_cfg)) return false;

    StationConfig updated_cfg = cfg1;
    updated_cfg.threshold_sm_mm = 15.0;
    updated_cfg.threshold_ml_mm = 25.0;
    if (!st1.set_config(updated_cfg)) return false;
    st1.feed(75.0, 21000);  // edge
    st1.feed(70.0, 21015);  // h = 30mm -> now L (was S under 40/60) -> Push at Station 1
    st1.feed(70.0, 21030);
    const DetectionResult r_dyn = st1.feed(70.0, 21045);
    if (!r_dyn.ready || r_dyn.size != Size::L || r_dyn.action != Action::Push) return false;

    // Independent per-station ranges with guard band (M=[7, 18], L=[24, 60], guard (18, 24) -> Pass).
    StationConfig range_cfg1 = cfg1;
    range_cfg1.use_ranges = true;
    range_cfg1.m_min_mm = 7.0;
    range_cfg1.m_max_mm = 18.0;
    range_cfg1.l_min_mm = 24.0;
    range_cfg1.l_max_mm = 60.0;
    range_cfg1.clear_samples = 1;
    if (!st1.set_config(range_cfg1)) return false;

    // Guard band item h = 21mm (d = 79mm) -> Size::S, Pass at Station 1
    st1.feed(85.0, 22000);
    st1.feed(79.0, 22015);
    st1.feed(79.0, 22030);
    const DetectionResult r_guard1 = st1.feed(79.0, 22045);
    if (!r_guard1.ready || r_guard1.size != Size::S || r_guard1.action != Action::Pass) return false;
    st1.feed(99.0, 22060);

    // L item h = 30mm (d = 70mm) -> Push at Station 1
    st1.feed(75.0, 22100);
    st1.feed(70.0, 22115);
    st1.feed(70.0, 22130);
    const DetectionResult r_range1_l = st1.feed(70.0, 22145);
    if (!r_range1_l.ready || r_range1_l.size != Size::L || r_range1_l.action != Action::Push) return false;

    // Station 2 with independent ranges: pushes M, misses L, passes guard band and S
    StationConfig range_cfg2 = range_cfg1;
    range_cfg2.station = 2;
    ItemDetector st2_range(range_cfg2);
    st2_range.feed(90.0, 100);
    st2_range.feed(87.0, 115);  // h = 13mm -> M -> Push
    st2_range.feed(87.0, 130);
    const DetectionResult r_range2_m = st2_range.feed(87.0, 145);
    if (!r_range2_m.ready || r_range2_m.size != Size::M || r_range2_m.action != Action::Push) return false;
    st2_range.feed(99.0, 160);

    st2_range.feed(85.0, 200);
    st2_range.feed(79.0, 215);  // h = 21mm -> guard band -> S -> Pass
    st2_range.feed(79.0, 230);
    const DetectionResult r_range2_g = st2_range.feed(79.0, 245);
    if (!r_range2_g.ready || r_range2_g.size != Size::S || r_range2_g.action != Action::Pass) return false;
    st2_range.feed(99.0, 260);

    st2_range.feed(75.0, 300);
    st2_range.feed(70.0, 315);  // h = 30mm -> L -> Missed at Station 2
    st2_range.feed(70.0, 330);
    const DetectionResult r_range2_l = st2_range.feed(70.0, 345);
    if (!r_range2_l.ready || r_range2_l.size != Size::L || r_range2_l.action != Action::Missed) return false;

    return true;
}

constexpr bool run_all_tests() noexcept {
    return test_classify_and_route() && test_height_and_median() && test_item_detector_sort_ac02();
}

static_assert(run_all_tests(), "sorter_core compile-time self-tests failed");

}  // namespace

int main() {
    assert(run_all_tests());
    // Runtime-only IEEE-754 nextafter boundary checks (std::nextafter is non-constexpr in C++17)
    assert(classify(std::nextafter(40.0, 60.0), 40.0, 60.0) == Size::M);
    assert(classify(std::nextafter(60.0, 100.0), 40.0, 60.0) == Size::L);
    return 0;
}

namespace peak_test {
// Item rises 82->47 over several samples then leaves: peak mode must see L (h~35), not the rising edge.
constexpr bool peak_mode_sees_tallest() {
    sorter_core::StationConfig c;
    c.station = 1; c.h0_mm = 88; c.detect_delta_mm = 6; c.clear_delta_mm = 5;
    c.use_ranges = true; c.l_min_mm = 35; c.l_max_mm = 100; c.m_min_mm = 25; c.m_max_mm = 35;
    c.n_samples = 3; c.clear_samples = 1; c.use_peak = true;
    sorter_core::ItemDetector d(c);
    const double seq[] = {73, 71, 65, 64, 56, 50, 49, 48, 49, 55, 88};
    sorter_core::DetectionResult r;
    for (double v : seq) { auto x = d.feed(v, 0); if (x.ready) r = x; }
    return r.ready && r.size == sorter_core::Size::L && r.action == sorter_core::Action::Push;
}
// Item falls away without the belt ever reading empty: must still decide early, on the peak.
constexpr bool peak_mode_decides_on_falling_edge() {
    sorter_core::StationConfig c;
    c.station = 1; c.h0_mm = 88; c.detect_delta_mm = 6; c.clear_delta_mm = 5;
    c.use_ranges = true; c.l_min_mm = 35; c.l_max_mm = 100; c.m_min_mm = 25; c.m_max_mm = 35;
    c.n_samples = 3; c.clear_samples = 1; c.use_peak = true;
    sorter_core::ItemDetector d(c);
    const double seq[] = {73, 65, 56, 50, 49, 48, 49, 60, 70, 75};
    for (double v : seq) { auto x = d.feed(v, 0); if (x.ready) return x.size == sorter_core::Size::L; }
    return false;
}
static_assert(peak_mode_sees_tallest());
static_assert(peak_mode_decides_on_falling_edge());
}  // namespace peak_test

namespace peak_timeout_test {
// Stale H0 (98) while the empty belt reads 83: peak mode must still decide after peak_timeout_ms.
constexpr bool peak_mode_times_out_on_stale_h0() {
    sorter_core::StationConfig c;
    c.station = 1; c.h0_mm = 98; c.detect_delta_mm = 6; c.clear_delta_mm = 5;
    c.use_ranges = true; c.l_min_mm = 28; c.l_max_mm = 100; c.m_min_mm = 18; c.m_max_mm = 28;
    c.n_samples = 3; c.clear_samples = 1; c.use_peak = true;
    sorter_core::ItemDetector d(c);
    for (std::uint32_t t = 0; t < 1999; t += 15)
        if (d.feed(83, t).ready) return false;
    return d.feed(83, 2010).ready;
}
static_assert(peak_mode_times_out_on_stale_h0());
}  // namespace peak_timeout_test
