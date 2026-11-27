// Compile-time verification (no EXE needed, works under Windows Application Control):
//   g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only tests/test_calib_math.cpp
// Full host binary (WSL/Linux/CI):
//   g++ -std=c++17 -Wall -Wextra -Werror tests/test_calib_math.cpp -o test_calib_math && ./test_calib_math
#include "../calib_math.h"

#include <cassert>

namespace {

using namespace sorter_core;

constexpr bool approx_eq(double a, double b, double eps = 1e-6) noexcept {
    return cabs(a - b) <= eps;
}

constexpr bool test_c1_baseline_calib_ac02() noexcept {
    const double stable[10] = {81.8, 82.2, 82.0, 82.1, 81.9, 82.0, 82.3, 81.7, 82.0, 82.0};
    const C1Result ok_res = compute_c1_baseline(stable, 10, 1.0);
    if (!ok_res.ok || ok_res.valid_count != 10) return false;
    if (!approx_eq(ok_res.h0_mm, 82.0, 1e-9)) return false;
    if (ok_res.sigma_mm > 1.0) return false;

    // Alternating +/-1.5mm yields sigma = 1.5mm > 1.0mm -> fail C1.
    const double noisy[6] = {80.5, 83.5, 80.5, 83.5, 80.5, 83.5};
    const C1Result bad_res = compute_c1_baseline(noisy, 6, 1.0);
    if (bad_res.ok || bad_res.sigma_mm <= 1.0) return false;

    return true;
}

constexpr bool test_c2_and_velocity_calib_ac03() noexcept {
    // raw_h = 1.05 * true_h - 0.5
    const C2Point pts[3] = {
        {10.0, 10.0},
        {30.0, 31.0},
        {50.0, 52.0},
    };
    const C2Result c2 = fit_c2_linear(pts, 3, 1.0);
    if (!c2.ok) return false;
    if (!approx_eq(c2.k, 1.05) || !approx_eq(c2.c, -0.5)) return false;
    if (c2.max_abs_residual_mm > 1.0) return false;

    if (!approx_eq(compute_v12_mm_s(100.0, 1000, 1500, 1200, 1700), 200.0)) return false;

    VelocityEstimator est;
    if (!est.update(200.0, VelocitySource::TofPair)) return false;
    // +35% outlier must be rejected cleanly.
    if (est.update(270.0, VelocitySource::TofPair)) return false;
    if (!approx_eq(est.velocity_mm_s(), 200.0)) return false;

    // Weighted fusion updates: 0.3, 0.1, 0.02
    if (!est.update(220.0, VelocitySource::TofPair)) return false;
    if (!approx_eq(est.velocity_mm_s(), 206.0)) return false;
    if (!est.update(216.0, VelocitySource::Occlusion)) return false;
    if (!approx_eq(est.velocity_mm_s(), 207.0)) return false;
    if (!est.update(200.0, VelocitySource::Model)) return false;
    if (!approx_eq(est.velocity_mm_s(), 206.86)) return false;

    return true;
}

constexpr bool test_voltage_comp_calib_ac05() noexcept {
    const DutyCompResult res = compute_compensated_duty(60.0, 12.0, 10.2);
    if (!res.valid || !res.voltage_low) return false;
    if (!approx_eq(res.u_cmd_pct, 60.0 * (12.0 / 10.2), 1e-4)) return false;

    const DutyCompResult sat = compute_compensated_duty(90.0, 12.0, 6.0);
    if (!sat.valid || !sat.voltage_low || sat.u_cmd_pct != 100.0) return false;

    const DutyCompResult nom = compute_compensated_duty(50.0, 12.0, 11.8);
    if (!nom.valid || nom.voltage_low) return false;

    return true;
}

constexpr bool test_h0_drift_calib_ac06() noexcept {
    if (evaluate_h0_drift(1.5) != DriftAction::AutoCompensate) return false;
    if (evaluate_h0_drift(-2.0) != DriftAction::AutoCompensate) return false;
    if (evaluate_h0_drift(2.5) != DriftAction::Warn) return false;
    if (evaluate_h0_drift(4.5) != DriftAction::RequireRecalibC1) return false;
    if (evaluate_h0_drift(-4.5) != DriftAction::RequireRecalibC1) return false;
    return true;
}

static_assert(test_c1_baseline_calib_ac02(), "CALIB-AC02 failed");
static_assert(test_c2_and_velocity_calib_ac03(), "CALIB-AC03 failed");
static_assert(test_voltage_comp_calib_ac05(), "CALIB-AC05 failed");
static_assert(test_h0_drift_calib_ac06(), "CALIB-AC06 failed");

}  // namespace

int main() {
    assert(test_c1_baseline_calib_ac02());
    assert(test_c2_and_velocity_calib_ac03());
    assert(test_voltage_comp_calib_ac05());
    assert(test_h0_drift_calib_ac06());
    return 0;
}
