#include "../belt_speed.h"
#include <cassert>
#include <cmath>
#include <limits>

namespace {
const SpeedConfig config{300, {{{100, 1, 0}, {104, 2, 4}}}, 4, 20, 35,
                         {60, 60, 60}, {12, 18, 24}, 4, 0, 12};

bool near(float a, float b) { return std::abs(a - b) < 0.001f; }

// The first height is intentionally misleading. The three interior samples contain one spike.
SpeedUpdate passage(BeltSpeedMonitor &monitor, unsigned station, uint32_t enter,
                    uint32_t duration, uint16_t interior) {
    const uint16_t clear = station == 1 ? 100 : 104;
    auto feed = [&](uint32_t t, uint16_t d) {
        return station == 1 ? monitor.update(t, d, {}) : monitor.update(t, {}, d);
    };
    feed(enter, 40);
    feed(enter + 20, interior);
    feed(enter + 40, 10);
    feed(enter + 60, interior);
    feed(enter + duration, clear);
    return feed(enter + duration + 20, clear);
}

void testBothEdgesAndSizeSpecificOccupancy() {
    BeltSpeedMonitor m(config);
    auto first = passage(m, 1, 0, 360, 80); // h=20, inclusive S boundary
    assert(first.station1->size == PartSize::S && near(*first.occupancy_mm_s, 200));
    auto medium = passage(m, 1, 500, 390, 65); // h=35, inclusive M boundary
    assert(medium.station1->size == PartSize::M && near(*medium.occupancy_mm_s, 200));
    auto large = passage(m, 1, 950, 420, 60);
    assert(large.station1->size == PartSize::L && near(*large.occupancy_mm_s, 200));
    auto small = passage(m, 2, 1500, 480, 60); // corrected h=20
    const float expected = 0.5f * (300000.0f / 1500 + 300000.0f / 1620);
    assert(small.station2->size == PartSize::S && near(*small.pair_mm_s, expected));
    assert(near(*m.speedMmPerSecond(), 200 + 0.3f * (expected - 200)));
    assert(m.source() == SpeedSource::tof_pair && *m.pairAgeMs() == 0);
    assert(!passage(m, 2, 2040, 390, 30).pair_mm_s); // M slipped through, never paired
    assert(near(*passage(m, 2, 2450, 420, 20).pair_mm_s, 200));
}

void testNoiseTimeoutAndOutlier() {
    BeltSpeedMonitor m(config);
    m.update(0, 80);
    m.update(20, 0);
    m.update(40, 100);
    assert(!m.update(60, 100).station1 && !m.speedMmPerSecond());
    passage(m, 1, 100, 360, 80);
    assert(!passage(m, 2, 5200, 360, 60).pair_mm_s); // expired
    passage(m, 1, 6000, 360, 80);
    assert(!passage(m, 2, 6750, 360, 60).pair_mm_s); // double speed, rejected
    assert(!m.pairAgeMs() && near(*m.speedMmPerSecond(), 200));
    m.update(40000);
    assert(!m.speedMmPerSecond() && m.source() == SpeedSource::none);
}

void testModelCadenceAndFallback() {
    BeltSpeedMonitor m(config);
    m.update(0, {}, {}, MotorReading{50, 12});
    m.update(99, {}, {}, MotorReading{50, 10.8f});
    assert(near(*m.modelMmPerSecond(), 200));
    m.update(100, {}, {}, MotorReading{50, 10.8f});
    assert(near(*m.modelMmPerSecond(), 180) && near(*m.speedMmPerSecond(), 199.6f));
    passage(m, 1, 200, 360, 80);
    assert(m.source() == SpeedSource::occupancy);
    passage(m, 2, 1700, 360, 60);
    for (uint32_t t = 2100; t <= 32200; t += 100) m.update(t, {}, {}, MotorReading{50, 10.8f});
    assert(*m.pairAgeMs() > 30000 && m.source() == SpeedSource::model);
    assert(std::abs(*m.speedMmPerSecond() - 180) < 0.1f);
    m.update(33000);
    assert(!m.modelMmPerSecond() && !m.speedMmPerSecond());
    m.update(34000, {}, {}, MotorReading{0, 12});
    assert(m.speedMmPerSecond() == 0);
    m.update(34100, {}, {}, MotorReading{50, 12});
    assert(m.speedMmPerSecond() == 200);
}

void testOccupancyOnlyAndModelDeviation() {
    BeltSpeedMonitor m(config);
    for (uint32_t start = 0; start <= 30000; start += 1000) {
        m.update(start, {}, {}, MotorReading{54, 12}); // 216 vs 200 measured
        auto result = passage(m, 1, start, 390, 65);
        assert(result.occupancy_mm_s && !result.pair_mm_s);
        if (start == 0) assert(near(*m.speedMmPerSecond(), 214.4f));
    }
    assert(m.source() == SpeedSource::occupancy && m.deviation() == SpeedDeviation::warning);
    m.update(31000, {}, {}, MotorReading{60, 12});
    passage(m, 1, 31000, 390, 65);
    assert(m.deviation() == SpeedDeviation::fault);
}

void testWraparound() {
    BeltSpeedMonitor m(config);
    const uint32_t start = std::numeric_limits<uint32_t>::max() - 1000;
    passage(m, 1, start, 360, 80);
    assert(near(*passage(m, 2, start + 1500, 360, 60).pair_mm_s, 200));
}
}

int main() {
    testBothEdgesAndSizeSpecificOccupancy();
    testNoiseTimeoutAndOutlier();
    testModelCadenceAndFallback();
    testOccupancyOnlyAndModelDeviation();
    testWraparound();
}
