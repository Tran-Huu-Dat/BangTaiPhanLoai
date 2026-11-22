#include "belt_speed.h"
#include <chrono>
#include <cstdio>
#include <string_view>
#include <thread>

namespace {
constexpr uint32_t sample_ms = 20;
// Illustrative calibration. The speed estimator receives raw ranges, PWM and voltage only.
const SpeedConfig config{300, {{{100, 1, 0}, {102, 1, 0}}}, 5, 40, 60,
                         {60, 60, 60}, {12, 12, 12}, 4.08f, 0, 12};

struct Part {
    PartSize size;
    int entry_mm;
    int height_mm;
};
constexpr Part parts[] = {{PartSize::M, 40, 50}, {PartSize::S, 160, 30},
                          {PartSize::L, 280, 70}, {PartSize::S, 400, 30}};

uint16_t reading(float travel_mm, unsigned station) {
    const float position = travel_mm - (station == 1 ? config.spacing_mm : 0);
    const float base = config.stations[station].belt_distance_mm;
    for (const Part &part : parts) {
        if (station == 1 && part.size == PartSize::M) continue;
        const auto index = static_cast<unsigned>(part.size);
        if (position >= part.entry_mm && position < part.entry_mm + config.length_mm[index] + config.spot_mm[index]) {
            return static_cast<uint16_t>(base - part.height_mm);
        }
    }
    return static_cast<uint16_t>(base);
}

const char *sourceName(SpeedSource source) {
    switch (source) {
        case SpeedSource::model: return "model";
        case SpeedSource::occupancy: return "tof1_occ";
        case SpeedSource::tof_pair: return "tof_pair";
        default: return "waiting";
    }
}

char sizeName(PartSize size) { return "SML"[static_cast<unsigned>(size)]; }

void simulate(bool once) {
    BeltSpeedMonitor monitor(config);
    uint32_t now = 0;
    float duty = 50;
    do {
        const float actual_mm_s = 4 * duty;
        std::printf("T1 day M | T2 day L | S di thang | L12 = 300 mm | v thuc mo phong = %.0f mm/s\n", actual_mm_s);
        // The simulated motor runs 2% slower than its calibration model.
        for (uint32_t elapsed = 0; elapsed <= 36000; elapsed += sample_ms, now += sample_ms) {
            const float travel = actual_mm_s * elapsed / 1000;
            const auto update = monitor.update(now, reading(travel, 0), reading(travel, 1), MotorReading{duty, 12});
            if (update.station1) {
                std::printf("T1 %c | %s", sizeName(update.station1->size),
                            update.station1->size == PartSize::M ? "day M" : "di tiep");
                if (update.occupancy_mm_s) std::printf(" | v_occ=%.1f mm/s", *update.occupancy_mm_s);
                std::puts("");
            }
            if (update.station2) {
                std::printf("T2 %c | %s", sizeName(update.station2->size),
                            update.station2->size == PartSize::L ? "day L" : "di thang");
                if (update.pair_mm_s) std::printf(" | v_pair=%.1f mm/s", *update.pair_mm_s);
                std::puts("");
            }
            if (elapsed % 1000 == 0) {
                std::printf("t=%.1fs | v_mm_s=%.1f | v_src=%s | v_model_mm_s=%.1f",
                            now / 1000.0, monitor.speedMmPerSecond().value_or(0),
                            sourceName(monitor.source()), monitor.modelMmPerSecond().value_or(0));
                if (auto age = monitor.pairAgeMs()) std::printf(" | v_age_s=%.2f", *age / 1000.0);
                else std::printf(" | v_age_s=--");
                if (monitor.deviation() == SpeedDeviation::warning) std::printf(" | CANH BAO lech toc do");
                if (monitor.deviation() == SpeedDeviation::fault) std::printf(" | FAULT lech toc do");
                std::puts("");
            }
            if (!once) std::this_thread::sleep_for(std::chrono::milliseconds(sample_ms));
        }
        duty = duty == 50 ? 55 : 50;
    } while (!once || duty != 50);
}
}

int main(int argc, char **argv) {
    const bool once = argc == 2 && std::string_view(argv[1]) == "--once";
    if (argc > 1 && !once) {
        std::fprintf(stderr, "Usage: simulate_belt_speed [--once]\n");
        return 1;
    }
    simulate(once);
}
