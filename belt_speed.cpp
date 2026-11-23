#include "belt_speed.h"
#include <algorithm>
#include <cassert>
#include <cmath>

namespace {
constexpr uint32_t measurement_lifetime_ms = 30000;
constexpr uint32_t model_period_ms = 100;
constexpr uint32_t motor_timeout_ms = 500;
}

BeltSpeedMonitor::BeltSpeedMonitor(const SpeedConfig &config) : config_(config) {
    assert(config.spacing_mm > 0 && config.max_transit_ms > 0);
    assert(config.median_samples >= 3 && config.median_samples <= 5);
    assert(config.presence_mm > 0 && config.presence_mm < config.threshold_sm_mm &&
           config.threshold_sm_mm < config.threshold_ml_mm);
    assert(config.pwm_slope >= 0 && (config.pwm_slope == 0 || config.nominal_volts > 0));
    for (const auto &station : config.stations) {
        assert(station.belt_distance_mm > 0 && station.gain > 0);
    }
    for (unsigned i = 0; i < config.length_mm.size(); ++i) {
        assert(config.length_mm[i] >= 0 && config.spot_mm[i] >= 0);
    }
}

std::optional<Passage> BeltSpeedMonitor::Detector::read(
    uint32_t now, std::optional<uint16_t> range, const TofCalibration &station,
    const SpeedConfig &config) {
    if (!range) return {};
    if (config.ejected_large_range_mm && !confirmed_large) {
        const auto &band = *config.ejected_large_range_mm;
        if (*range >= band[0] && *range <= band[1]) {
            if (first_large) confirmed_large = first_large;
            else first_large = now;
        } else {
            first_large.reset();
        }
    }
    if (*range == 0 || *range > 4000) {
        clear.reset();
        return {};
    }
    const float height = (station.belt_distance_mm - *range - station.offset_mm) / station.gain;
    if (height >= config.presence_mm) {
        clear.reset();
        if (!enter) enter = now; // Keep t0, omit this mixed edge sample from the median.
        else if (count < config.median_samples && count < heights.size()) heights[count++] = height;
        return {};
    }
    if (!enter) return {};
    if (!clear) {
        clear = now; // Confirm with the next clear sample but keep the first edge time.
        return {};
    }
    std::optional<Passage> event;
    if (confirmed_large) {
        event = Passage{PartSize::L, *confirmed_large, *clear};
    } else if (count >= 3 && count == config.median_samples) {
        std::sort(heights.begin(), heights.begin() + count);
        const float median = (heights[(count - 1) / 2] + heights[count / 2]) / 2;
        const PartSize size = median <= config.threshold_sm_mm ? PartSize::S :
                              median <= config.threshold_ml_mm ? PartSize::M : PartSize::L;
        if (size != PartSize::L || !config.ejected_large_range_mm)
            event = Passage{size, *enter, *clear};
    }
    *this = {};
    return event;
}

bool BeltSpeedMonitor::blend(float sample, float alpha) {
    if (!std::isfinite(sample) || sample <= 0) return false;
    if (estimate_ && *estimate_ > 0 && std::abs(sample - *estimate_) > 0.2f * *estimate_) {
        return false;
    }
    estimate_ = estimate_ && *estimate_ > 0 ? *estimate_ + alpha * (sample - *estimate_) : sample;
    last_update_ = now_;
    return true;
}

void BeltSpeedMonitor::updateMotor(const MotorReading &reading) {
    if (config_.pwm_slope == 0) return;
    if (!(reading.duty_percent >= 0 && reading.duty_percent <= 100 &&
          std::isfinite(reading.supply_volts) && reading.supply_volts > 0)) return;
    if (last_model_ && now_ - *last_model_ < model_period_ms) return;
    last_model_ = now_;
    motor_stopped_ = reading.duty_percent == 0;
    if (motor_stopped_) {
        estimate_ = model_ = 0;
        last_update_ = now_;
        last_pair_.reset();
        last_occupancy_.reset();
        last_comparison_.reset();
        deviation_since_.reset();
        deviation_ = SpeedDeviation::normal;
        pending_.clear();
        first_ = {};
        second_ = {};
        return;
    }
    model_ = std::max(0.0f, config_.pwm_slope * reading.duty_percent * reading.supply_volts /
                             config_.nominal_volts + config_.pwm_intercept);
    blend(*model_, 0.02f);
}

void BeltSpeedMonitor::compareWithModel(float measured) {
    if (!model_ || *model_ <= 0) return;
    last_comparison_ = now_;
    const float error = std::abs(measured - *model_) / *model_;
    if (error <= 0.05f) {
        deviation_since_.reset();
        deviation_ = SpeedDeviation::normal;
        return;
    }
    if (!deviation_since_) deviation_since_ = now_;
    if (error > 0.10f) deviation_ = SpeedDeviation::fault;
    else if (now_ - *deviation_since_ >= measurement_lifetime_ms) deviation_ = SpeedDeviation::warning;
    else deviation_ = SpeedDeviation::normal;
}

std::optional<uint32_t> BeltSpeedMonitor::pairAgeMs() const {
    if (last_pair_) return now_ - *last_pair_;
    return {};
}

SpeedSource BeltSpeedMonitor::source() const {
    if (!estimate_) return SpeedSource::none;
    if (last_pair_ && now_ - *last_pair_ <= measurement_lifetime_ms) return SpeedSource::tof_pair;
    if (last_occupancy_ && now_ - *last_occupancy_ <= measurement_lifetime_ms) return SpeedSource::occupancy;
    return model_ ? SpeedSource::model : SpeedSource::none;
}

SpeedUpdate BeltSpeedMonitor::update(uint32_t now_ms, std::optional<uint16_t> tof1_mm,
                                     std::optional<uint16_t> tof2_mm, std::optional<MotorReading> motor) {
    now_ = now_ms;
    if (last_comparison_ && now_ - *last_comparison_ > measurement_lifetime_ms) {
        last_comparison_.reset();
        deviation_since_.reset();
        deviation_ = SpeedDeviation::normal;
    }
    if (last_update_ && now_ - *last_update_ > measurement_lifetime_ms) estimate_.reset();
    if (last_model_ && now_ - *last_model_ > motor_timeout_ms) {
        model_.reset();
        last_model_.reset();
        if (source() == SpeedSource::none) estimate_.reset();
    }
    if (motor) updateMotor(*motor);
    if (source() == SpeedSource::none) estimate_.reset();
    SpeedUpdate result{};
    if (model_ && motor_stopped_) return result;
    result.station1 = first_.read(now_, tof1_mm, config_.stations[0], config_);
    result.station2 = second_.read(now_, tof2_mm, config_.stations[1], config_);

    // Allow the second clear sample to confirm an exit exactly at the transit limit.
    const auto exit_candidate = result.station2 ? std::optional<uint32_t>(result.station2->exit_ms) : second_.clear;
    const uint32_t expiry_time = exit_candidate && now_ - *exit_candidate <= config_.max_transit_ms
                                     ? *exit_candidate : now_;
    while (!pending_.empty() && now_ - pending_.front().exit_ms > config_.max_transit_ms &&
           expiry_time - pending_.front().exit_ms > config_.max_transit_ms) {
        pending_.pop_front();
    }
    if (result.station1) {
        const auto &part = *result.station1;
        if (part.size != PartSize::M) pending_.push_back(part);
        const auto index = static_cast<unsigned>(part.size);
        const uint32_t duration_ms = part.exit_ms - part.enter_ms;
        if (duration_ms > 0 && config_.length_mm[index] > 0) {
            const float v = 1000 * (config_.length_mm[index] + config_.spot_mm[index]) / duration_ms;
            compareWithModel(v);
            if (blend(v, 0.1f)) {
                result.occupancy_mm_s = v;
                last_occupancy_ = now_;
            }
        }
    }
    if (result.station2 && !pending_.empty()) {
        const auto &arrival = *result.station2;
        const auto &departure = pending_.front();
        if (arrival.size != departure.size) return result;
        const uint32_t entry_dt = arrival.enter_ms - departure.enter_ms;
        const uint32_t exit_dt = arrival.exit_ms - departure.exit_ms;
        const bool entry_only = arrival.size == PartSize::L && config_.ejected_large_range_mm.has_value();
        if (entry_dt && entry_dt <= config_.max_transit_ms &&
            (entry_only || (exit_dt && exit_dt <= config_.max_transit_ms))) {
            const float entry_speed = 1000 * config_.spacing_mm / entry_dt;
            const float v = entry_only ? entry_speed : 0.5f * (entry_speed + 1000 * config_.spacing_mm / exit_dt);
            pending_.pop_front();
            compareWithModel(v);
            if (blend(v, 0.3f)) {
                result.pair_mm_s = v;
                last_pair_ = now_;
            }
        }
    }
    return result;
}
