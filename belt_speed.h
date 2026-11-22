#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>

enum class PartSize { S, M, L };
enum class SpeedSource { none, model, occupancy, tof_pair };
enum class SpeedDeviation { normal, warning, fault };

struct TofCalibration {
    float belt_distance_mm, gain, offset_mm;
};

struct SpeedConfig {
    float spacing_mm;
    std::array<TofCalibration, 2> stations;
    float presence_mm, threshold_sm_mm, threshold_ml_mm;
    std::array<float, 3> length_mm, spot_mm; // S, M, L in order, from calibration C3
    float pwm_slope, pwm_intercept, nominal_volts; // duty in percent
    unsigned median_samples = 3; // 3 to 5 interior samples after the leading edge
    uint32_t max_transit_ms = 5000;
};

struct MotorReading {
    float duty_percent, supply_volts;
};

struct Passage {
    PartSize size;
    uint32_t enter_ms, exit_ms;
};

struct SpeedUpdate {
    std::optional<Passage> station1, station2;
    std::optional<float> occupancy_mm_s, pair_mm_s; // populated only on accepted measurements
};

class BeltSpeedMonitor {
public:
    explicit BeltSpeedMonitor(const SpeedConfig &config);
    // Poll with monotonic milliseconds modulo 2^32. nullopt = no fresh sample, 0 = bad range.
    SpeedUpdate update(uint32_t now_ms, std::optional<uint16_t> tof1_mm = {},
                       std::optional<uint16_t> tof2_mm = {}, std::optional<MotorReading> motor = {});
    std::optional<float> speedMmPerSecond() const { return estimate_; }
    std::optional<float> modelMmPerSecond() const { return model_; }
    std::optional<uint32_t> pairAgeMs() const;
    SpeedSource source() const;
    SpeedDeviation deviation() const { return deviation_; }

private:
    struct Detector {
        std::optional<uint32_t> enter, clear;
        std::array<float, 5> heights{};
        unsigned count = 0;
        std::optional<Passage> read(uint32_t now, std::optional<uint16_t> range,
                                    const TofCalibration &station, const SpeedConfig &config);
    };
    bool blend(float sample, float alpha);
    void updateMotor(const MotorReading &reading);
    void compareWithModel(float measured);
    SpeedConfig config_;
    Detector first_, second_;
    std::deque<Passage> pending_;
    uint32_t now_ = 0;
    std::optional<uint32_t> last_pair_, last_occupancy_, last_model_, last_update_;
    std::optional<float> estimate_, model_;
    std::optional<uint32_t> deviation_since_, last_comparison_;
    SpeedDeviation deviation_ = SpeedDeviation::normal;
    bool motor_stopped_ = false;
};
