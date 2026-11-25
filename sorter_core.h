#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace sorter_core {

enum class Size { Invalid, S, M, L };
enum class Action { Invalid, Pass, Push, Missed };

constexpr std::size_t MAX_MEDIAN_SAMPLES = 9;

constexpr bool is_finite(double x) noexcept {
    constexpr double inf = std::numeric_limits<double>::infinity();
    return x == x && x != inf && x != -inf;
}

// SORT-R04: caller supplies a calibrated height for an already detected item.
constexpr Size classify(double height, double threshold_sm, double threshold_ml) noexcept {
    if (!is_finite(height) || !is_finite(threshold_sm) ||
        !is_finite(threshold_ml) || threshold_sm >= threshold_ml)
        return Size::Invalid;
    return height <= threshold_sm ? Size::S : height <= threshold_ml ? Size::M : Size::L;
}

// D-14: Independent per-station height ranges for L (Tram 1) and M (Tram 2);
// guard bands or values outside both ranges return Size::S (pass through).
constexpr Size classify_ranges(double height, double m_min, double m_max,
                               double l_min, double l_max) noexcept {
    if (!is_finite(height) || !is_finite(m_min) || !is_finite(m_max) ||
        !is_finite(l_min) || !is_finite(l_max) ||
        m_min < 0.0 || l_min < 0.0 || m_min >= m_max || l_min >= l_max) {
        return Size::Invalid;
    }
    if (height >= l_min && height <= l_max) return Size::L;
    if (height >= m_min && height <= m_max) return Size::M;
    return Size::S;
}

// SORT-R05 / D-13: Trạm 1 pushes L, Trạm 2 pushes M, S passes straight (no push).
// Missed must be reported by the caller, never actuated.
constexpr Action route(Size size, unsigned station) noexcept {
    if (station != 1 && station != 2) return Action::Invalid;
    switch (size) {
        case Size::S: return Action::Pass;
        case Size::L: return station == 1 ? Action::Push : Action::Missed;
        case Size::M: return station == 2 ? Action::Push : Action::Pass;
        default: return Action::Invalid;
    }
}

// SORT-R03 / C2: h = ((H0 - d) - c) / k (k=1, c=0 before linear calibration).
constexpr double calibrate_height(double distance_mm, double h0_mm,
                                  double k = 1.0, double c = 0.0) noexcept {
    if (!is_finite(distance_mm) || !is_finite(h0_mm) ||
        !is_finite(k) || !is_finite(c) || k <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return ((h0_mm - distance_mm) - c) / k;
}

// SORT-R03: deterministic fixed-buffer median without heap allocation.
constexpr double median_of(const double* samples, std::size_t count) noexcept {
    if (samples == nullptr || count == 0 || count > MAX_MEDIAN_SAMPLES) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double buf[MAX_MEDIAN_SAMPLES]{};
    for (std::size_t i = 0; i < count; ++i) {
        if (!is_finite(samples[i])) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        buf[i] = samples[i];
    }
    for (std::size_t i = 1; i < count; ++i) {
        double key = buf[i];
        std::size_t j = i;
        while (j > 0 && buf[j - 1] > key) {
            buf[j] = buf[j - 1];
            --j;
        }
        buf[j] = key;
    }
    if ((count & 1U) != 0U) {
        return buf[count / 2];
    }
    return 0.5 * (buf[count / 2 - 1] + buf[count / 2]);
}

struct StationConfig {
    unsigned station = 1;
    double h0_mm = 100.0;
    double detect_delta_mm = 5.0;
    double clear_delta_mm = 5.0;
    double k = 1.0;
    double c = 0.0;
    double threshold_sm_mm = 40.0;
    double threshold_ml_mm = 60.0;
    bool use_ranges = false;
    double m_min_mm = 7.0;
    double m_max_mm = 20.0;
    double l_min_mm = 22.0;
    double l_max_mm = 100.0;
    std::size_t n_samples = 3;
    std::size_t clear_samples = 2;
    bool discard_first_edge_sample = true;
    double max_valid_distance_mm = 4000.0;
    // Decide when the item leaves the beam, using its highest plateau (lowest windowed median)
    // instead of the first n samples, which only see the rising edge of a moving item.
    bool use_peak = false;
    // use_peak: decide as soon as the windowed median rises this far above the peak (item falling away).
    double peak_drop_mm = 8.0;
    // use_peak: decide anyway after this long under the beam (stale H0 / item parked under the ToF).
    std::uint32_t peak_timeout_ms = 2000;
};

constexpr bool config_valid(const StationConfig& cfg) noexcept {
    const bool bounds_ok = cfg.use_ranges
        ? (is_finite(cfg.m_min_mm) && is_finite(cfg.m_max_mm) &&
           cfg.m_min_mm >= 0.0 && cfg.m_min_mm < cfg.m_max_mm &&
           is_finite(cfg.l_min_mm) && is_finite(cfg.l_max_mm) &&
           cfg.l_min_mm >= 0.0 && cfg.l_min_mm < cfg.l_max_mm)
        : (is_finite(cfg.threshold_sm_mm) && is_finite(cfg.threshold_ml_mm) &&
           cfg.threshold_sm_mm < cfg.threshold_ml_mm);
    return (cfg.station == 1 || cfg.station == 2) &&
           is_finite(cfg.h0_mm) && cfg.h0_mm > 0.0 &&
           is_finite(cfg.detect_delta_mm) && cfg.detect_delta_mm > 0.0 &&
           is_finite(cfg.clear_delta_mm) && cfg.clear_delta_mm >= 0.0 &&
           cfg.clear_delta_mm <= cfg.detect_delta_mm &&
           is_finite(cfg.k) && cfg.k > 0.0 &&
           is_finite(cfg.c) &&
           bounds_ok &&
           cfg.n_samples >= 1 && cfg.n_samples <= MAX_MEDIAN_SAMPLES &&
           cfg.clear_samples >= 1 &&
           is_finite(cfg.max_valid_distance_mm) && cfg.max_valid_distance_mm > cfg.h0_mm;
}

struct DetectionResult {
    bool ready = false;
    std::uint32_t t0_ms = 0;
    double median_distance_mm = 0.0;
    double height_mm = 0.0;
    Size size = Size::Invalid;
    Action action = Action::Invalid;
};

// SORT-R02..R05 / SORT-AC02: per-station item detection, edge-sample discard,
// median filtering, height calculation, D-11 routing, and WaitClear hysteresis.
class ItemDetector {
public:
    enum class State { Idle, Collecting, WaitClear };

    constexpr explicit ItemDetector(const StationConfig& cfg) noexcept : cfg_(cfg) {}

    constexpr State state() const noexcept { return state_; }

    constexpr bool set_config(const StationConfig& cfg) noexcept {
        if (!config_valid(cfg)) return false;
        cfg_ = cfg;
        reset();
        return true;
    }

    constexpr void reset() noexcept {
        state_ = State::Idle;
        t0_ms_ = 0;
        sample_count_ = 0;
        clear_count_ = 0;
        have_best_ = false;
    }

    constexpr DetectionResult feed(double distance_mm, std::uint32_t now_ms) noexcept {
        if (!config_valid(cfg_)) return {};
        // Invalid/error samples never trigger detection and never unlock WaitClear (D-03).
        if (!is_finite(distance_mm) || distance_mm <= 0.0 ||
            distance_mm > cfg_.max_valid_distance_mm) {
            return {};
        }

        const double detect_below = cfg_.h0_mm - cfg_.detect_delta_mm;
        const double clear_at_or_above = cfg_.h0_mm - cfg_.clear_delta_mm;

        switch (state_) {
            case State::Idle: {
                if (distance_mm < detect_below) {
                    t0_ms_ = now_ms;
                    sample_count_ = 0;
                    state_ = State::Collecting;
                    if (!cfg_.discard_first_edge_sample) {
                        samples_[sample_count_++] = distance_mm;
                        if (sample_count_ == cfg_.n_samples) {
                            return finalize_(median_of(samples_, sample_count_));
                        }
                    }
                }
                return {};
            }
            case State::Collecting: {
                if (distance_mm >= clear_at_or_above) {
                    if (cfg_.use_peak && have_best_) return finalize_(best_median_);
                    // Object left before N valid post-edge samples -> ignore glitch.
                    reset();
                    return {};
                }
                if (cfg_.use_peak) {
                    if (have_best_ && now_ms - t0_ms_ >= cfg_.peak_timeout_ms) return finalize_(best_median_);
                    if (sample_count_ == cfg_.n_samples) {
                        for (std::size_t i = 1; i < sample_count_; ++i) samples_[i - 1] = samples_[i];
                        --sample_count_;
                    }
                    samples_[sample_count_++] = distance_mm;
                    if (sample_count_ == cfg_.n_samples) {
                        const double m = median_of(samples_, sample_count_);
                        if (!have_best_ || m < best_median_) {
                            best_median_ = m;
                            have_best_ = true;
                        } else if (m >= best_median_ + cfg_.peak_drop_mm) {
                            return finalize_(best_median_);
                        }
                    }
                    return {};
                }
                samples_[sample_count_++] = distance_mm;
                if (sample_count_ == cfg_.n_samples) {
                    return finalize_(median_of(samples_, sample_count_));
                }
                return {};
            }
            case State::WaitClear: {
                if (distance_mm >= clear_at_or_above) {
                    ++clear_count_;
                    if (clear_count_ >= cfg_.clear_samples) {
                        reset();
                    }
                } else {
                    clear_count_ = 0;
                }
                return {};
            }
        }
        return {};
    }

private:
    constexpr DetectionResult finalize_(double med_d) noexcept {
        const double h = calibrate_height(med_d, cfg_.h0_mm, cfg_.k, cfg_.c);
        const Size sz = cfg_.use_ranges
            ? classify_ranges(h, cfg_.m_min_mm, cfg_.m_max_mm, cfg_.l_min_mm, cfg_.l_max_mm)
            : classify(h, cfg_.threshold_sm_mm, cfg_.threshold_ml_mm);
        const Action act = route(sz, cfg_.station);
        const std::uint32_t t0 = t0_ms_;
        state_ = State::WaitClear;
        sample_count_ = 0;
        clear_count_ = 0;
        return DetectionResult{true, t0, med_d, h, sz, act};
    }

    StationConfig cfg_;
    State state_ = State::Idle;
    std::uint32_t t0_ms_ = 0;
    double samples_[MAX_MEDIAN_SAMPLES]{};
    std::size_t sample_count_ = 0;
    std::size_t clear_count_ = 0;
    double best_median_ = 0.0;
    bool have_best_ = false;
};

}  // namespace sorter_core
