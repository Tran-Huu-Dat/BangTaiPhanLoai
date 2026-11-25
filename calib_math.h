#pragma once

#include "sorter_core.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace sorter_core {

constexpr double cabs(double x) noexcept { return x < 0.0 ? -x : x; }

constexpr double csqrt(double x) noexcept {
    if (!is_finite(x) || x < 0.0) return std::numeric_limits<double>::quiet_NaN();
    if (x == 0.0) return 0.0;
    double r = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 20; ++i) {
        r = 0.5 * (r + x / r);
    }
    return r;
}

constexpr std::size_t MAX_C1_SAMPLES = 256;

struct C1Result {
    bool ok = false;
    double h0_mm = 0.0;
    double mean_mm = 0.0;
    double sigma_mm = 0.0;
    std::size_t valid_count = 0;
};

// CALIB-R02 / CALIB-AC02: empty-belt baseline median H0 and noise sigma0.
constexpr C1Result compute_c1_baseline(const double* samples, std::size_t count,
                                       double max_sigma_mm = 1.0) noexcept {
    if (samples == nullptr || count == 0 || !is_finite(max_sigma_mm) || max_sigma_mm < 0.0) {
        return {};
    }
    double buf[MAX_C1_SAMPLES]{};
    std::size_t n = 0;
    double sum = 0.0;
    for (std::size_t i = 0; i < count && n < MAX_C1_SAMPLES; ++i) {
        if (is_finite(samples[i]) && samples[i] > 0.0) {
            buf[n++] = samples[i];
            sum += samples[i];
        }
    }
    if (n == 0) return {};

    for (std::size_t i = 1; i < n; ++i) {
        const double key = buf[i];
        std::size_t j = i;
        while (j > 0 && buf[j - 1] > key) {
            buf[j] = buf[j - 1];
            --j;
        }
        buf[j] = key;
    }

    const double median_mm =
        (n & 1U) != 0U ? buf[n / 2] : 0.5 * (buf[n / 2 - 1] + buf[n / 2]);
    const double mean_mm = sum / static_cast<double>(n);
    double var_sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = buf[i] - mean_mm;
        var_sum += d * d;
    }
    const double sigma_mm = n >= 2 ? csqrt(var_sum / static_cast<double>(n)) : 0.0;
    const bool ok = is_finite(sigma_mm) && sigma_mm <= max_sigma_mm;
    return C1Result{ok, median_mm, mean_mm, sigma_mm, n};
}

struct C2Point {
    double true_h_mm = 0.0;
    double raw_h_mm = 0.0;
};

struct C2Result {
    bool ok = false;
    double k = 1.0;
    double c = 0.0;
    double max_abs_residual_mm = 0.0;
};

// CALIB-R03 / CALIB-AC03: ordinary least-squares fit raw_h = k * true_h + c.
constexpr C2Result fit_c2_linear(const C2Point* pts, std::size_t count,
                                 double max_residual_mm = 1.0) noexcept {
    if (pts == nullptr || count < 2 || !is_finite(max_residual_mm) || max_residual_mm < 0.0) {
        return {false, 1.0, 0.0, std::numeric_limits<double>::quiet_NaN()};
    }
    double sum_x = 0.0;
    double sum_y = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        if (!is_finite(pts[i].true_h_mm) || !is_finite(pts[i].raw_h_mm)) {
            return {false, 1.0, 0.0, std::numeric_limits<double>::quiet_NaN()};
        }
        sum_x += pts[i].true_h_mm;
        sum_y += pts[i].raw_h_mm;
    }
    const double mx = sum_x / static_cast<double>(count);
    const double my = sum_y / static_cast<double>(count);

    double num = 0.0;
    double den = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double dx = pts[i].true_h_mm - mx;
        const double dy = pts[i].raw_h_mm - my;
        num += dx * dy;
        den += dx * dx;
    }
    if (den <= 1e-12) {
        return {false, 1.0, 0.0, std::numeric_limits<double>::quiet_NaN()};
    }
    const double k = num / den;
    if (!is_finite(k) || k <= 0.0) {
        return {false, 1.0, 0.0, std::numeric_limits<double>::quiet_NaN()};
    }
    const double c = my - k * mx;

    double max_res = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double h_cal = (pts[i].raw_h_mm - c) / k;
        const double res = cabs(h_cal - pts[i].true_h_mm);
        if (res > max_res) max_res = res;
    }
    return C2Result{max_res <= max_residual_mm, k, c, max_res};
}

enum class VelocitySource { TofPair, Occlusion, Model };

constexpr double velocity_alpha(VelocitySource src) noexcept {
    switch (src) {
        case VelocitySource::TofPair: return 0.30;
        case VelocitySource::Occlusion: return 0.10;
        case VelocitySource::Model: return 0.02;
    }
    return 0.0;
}

// CALIB-R08: two-ToF velocity average from entry (t0) and exit (t1) transit times.
constexpr double compute_v12_mm_s(double l12_mm,
                                    std::uint32_t t0_1_ms, std::uint32_t t0_2_ms,
                                    std::uint32_t t1_1_ms, std::uint32_t t1_2_ms) noexcept {
    const std::int32_t dt0 = static_cast<std::int32_t>(t0_2_ms - t0_1_ms);
    const std::int32_t dt1 = static_cast<std::int32_t>(t1_2_ms - t1_1_ms);
    if (!is_finite(l12_mm) || l12_mm <= 0.0 || dt0 <= 0 || dt1 <= 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double v0 = (l12_mm * 1000.0) / static_cast<double>(dt0);
    const double v1 = (l12_mm * 1000.0) / static_cast<double>(dt1);
    return 0.5 * (v0 + v1);
}

// CALIB-R08 / CALIB-AC03: weighted velocity fusion with +/-20% outlier rejection.
class VelocityEstimator {
public:
    constexpr explicit VelocityEstimator(double outlier_fraction = 0.20) noexcept
        : outlier_fraction_(outlier_fraction) {}

    constexpr bool initialized() const noexcept { return initialized_; }
    constexpr double velocity_mm_s() const noexcept { return v_mm_s_; }

    constexpr bool update(double measured_v_mm_s, VelocitySource src) noexcept {
        if (!is_finite(measured_v_mm_s) || measured_v_mm_s <= 0.0) return false;
        if (!initialized_) {
            v_mm_s_ = measured_v_mm_s;
            initialized_ = true;
            return true;
        }
        const double rel_diff = cabs(measured_v_mm_s - v_mm_s_) / v_mm_s_;
        if (!is_finite(outlier_fraction_) || rel_diff > outlier_fraction_) {
            return false;
        }
        v_mm_s_ += velocity_alpha(src) * (measured_v_mm_s - v_mm_s_);
        return true;
    }

private:
    bool initialized_ = false;
    double v_mm_s_ = 0.0;
    double outlier_fraction_ = 0.20;
};

struct DutyCompResult {
    double u_cmd_pct = 0.0;
    bool voltage_low = false;
    bool valid = false;
};

// CALIB-R09 / CALIB-AC05: supply voltage drop compensation saturated to [0, 100]%.
constexpr DutyCompResult compute_compensated_duty(double u_target_pct, double v_nom,
                                                  double v_s) noexcept {
    if (!is_finite(u_target_pct) || !is_finite(v_nom) || !is_finite(v_s) ||
        u_target_pct < 0.0 || v_nom <= 0.0 || v_s <= 0.0) {
        return {};
    }
    double u = u_target_pct * (v_nom / v_s);
    if (u < 0.0) u = 0.0;
    if (u > 100.0) u = 100.0;
    return DutyCompResult{u, v_s < 0.90 * v_nom, true};
}

enum class DriftAction { AutoCompensate, Warn, RequireRecalibC1 };

// CALIB-R10 / CALIB-AC06: H0 drift health thresholds (<=2mm auto, <=4mm warn, >4mm C1).
constexpr DriftAction evaluate_h0_drift(double delta_mm) noexcept {
    const double abs_d = cabs(delta_mm);
    if (abs_d <= 2.0) return DriftAction::AutoCompensate;
    if (abs_d <= 4.0) return DriftAction::Warn;
    return DriftAction::RequireRecalibC1;
}

}  // namespace sorter_core
