/**
 * =====================================================================
 *  BANG TAI PHAN LOAI TU DONG - Raspberry Pi Pico 2 W (VL53L1X)
 * =====================================================================
 *  Phan cung:
 *    - ToF 1 (VL53L1X) : I2C0  SDA=GP0, SCL=GP1, LPN=3V3 -> Tram 1
 *    - ToF 2 (VL53L1X) : I2C1  SDA=GP6, SCL=GP7, LPN=3V3 -> Tram 2
 *    - Solenoid 1      : GP10 (HIGH = hut, LOW = nha) qua L298N IN1
 *    - Solenoid 2      : GP11 (HIGH = hut, LOW = nha) qua L298N IN3
 *
 *  Logic:
 *    - 2 tram chay DOC LAP va DONG THOI, khong chan CPU (Non-blocking).
 *    - Tram 1: size M, 65-75mm -> kich Solenoid 1 trong 500ms roi nha.
 *    - Tram 2: size L, 50-60mm -> kich Solenoid 2 trong 500ms roi nha.
 *    - Size S di qua; mat bang tai 85-90mm khong kich solenoid.
 *    - L298N: IN2/IN4 noi GND, ENA/ENB bat, GND chung voi Pico.
 *    - Co bo dem san pham phan loai, loc nhieu (debounce), chong hut lap.
 * =====================================================================
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"
#include "vl53l1x.h"
#include "belt_speed.h"

// ======================= CAU HINH CHAN =======================
#define TOF1_I2C        i2c0
#define TOF1_SDA_PIN    0
#define TOF1_SCL_PIN    1

#define TOF2_I2C        i2c1
#define TOF2_SDA_PIN    6
#define TOF2_SCL_PIN    7

#define SOL1_PIN        10
#define SOL2_PIN        11

#define I2C_BAUDRATE    (100 * 1000)   // 100 kHz, da thu voi hai cam bien

// ======================= THAM SO HE THONG =======================
#define BELT_CLEAR_MIN_MM   80     // Mat bang tai 85-90mm; >= 80mm moi cho nhan phoi tiep
#define DEBOUNCE_COUNT      2      // So mau lien tiep trong khoang size de xac nhan phoi
#define CLEAR_COUNT         2      // So mau lien tiep thay mat bang tai de chong kich lap
#define SENSOR_RETRY_MS     2500   // Thu khoi tao lai cam bien neu mat ket noi
#define SENSOR_TIMEOUT_MS   800    // Timeout tin hieu cam bien
#define PRINT_PERIOD_MS     250    // Chu ky in log ra Serial Monitor
#define QUEUE_SIZE          8      // So luong vat toi da trong hang doi tre di chuyen
#define MAX_VALID_RANGE_MM  4000   // Khoang cach do toi da hop le cua VL53L1X (4m)

// ======================= KIEU DU LIEU =======================
enum StationMode {
    MODE_PULSE,   // Kich Solenoid trong on_time_ms roi nha
    MODE_HOLD     // Kich Solenoid giu cho den khi vat di qua
};

enum StationState {
    ST_IDLE,        // Cho vat
    ST_ACTIVE,      // Solenoid dang hut
    ST_WAIT_CLEAR   // Cho vat roi khoi vung do truoc khi nhan vat tiep theo
};

struct Station {
    // --- Cau hinh tram ---
    const char   *name;
    i2c_inst_t   *i2c;
    uint          sda_pin, scl_pin, sol_pin;
    StationMode   mode;
    uint16_t      min_mm, max_mm;  // Khoang cach nhan size, bao gom hai dau mut (mm)
    uint32_t      travel_delay_ms; // Thoi gian vat chay tu cam bien -> tay gat (0 = gat ngay)
    uint32_t      on_time_ms;      // Thoi gian Solenoid hut (ms)

    // --- Trang thai runtime ---
    VL53L1X      *tof;
    bool          sensor_ok;
    bool          sensor_initialized;
    bool          has_valid_target;
    uint16_t      distance_mm;
    uint32_t      last_data_ms;
    uint32_t      last_retry_ms;
    uint8_t       hit_count;
    uint8_t       clear_count;
    StationState  state;
    uint32_t      state_start_ms;
    uint32_t      total_count;

    // --- Hang doi tre di chuyen (FIFO queue) ---
    uint32_t      queue_trigger_time[QUEUE_SIZE];
    uint8_t       q_head;
    uint8_t       q_tail;
    uint8_t       q_count;
};

static VL53L1X tof1(TOF1_I2C);
static VL53L1X tof2(TOF2_I2C);

// Cấu hình 2 trạm:
// Trạm 1: size M 65-75mm, kích ngay tại trạm trong 500ms rồi nhả.
// Trạm 2: size L 50-60mm, kích ngay tại trạm trong 500ms rồi nhả.
// Size S nằm ngoài hai khoảng này nên đi qua cả hai trạm.
static Station stations[2] = {
    { "TRAM1", TOF1_I2C, TOF1_SDA_PIN, TOF1_SCL_PIN, SOL1_PIN, MODE_PULSE, 65, 75, 0, 500, &tof1 },
    { "TRAM2", TOF2_I2C, TOF2_SDA_PIN, TOF2_SCL_PIN, SOL2_PIN, MODE_PULSE, 50, 60, 0, 500, &tof2 },
};

// Physical belt: 85-90 mm empty, M at 65-75 mm, L at 50-60 mm.
// No calibrated effective part lengths or motor PWM/voltage inputs yet.
static const SpeedConfig speed_config{
    300, {{{88, 1, 0}, {88, 1, 0}}}, 4, 12, 25,
    {}, {}, 0, 0, 0, 3, 30000,
    std::array<uint16_t, 2>{stations[1].min_mm, stations[1].max_mm}
};

// ======================= TIEN ICH =======================
static inline uint32_t millis() {
    return to_ms_since_boot(get_absolute_time());
}

static void queuePush(Station &s, uint32_t trigger_at) {
    if (s.q_count < QUEUE_SIZE) {
        s.queue_trigger_time[s.q_tail] = trigger_at;
        s.q_tail = (s.q_tail + 1) % QUEUE_SIZE;
        s.q_count++;
    }
}

static bool queuePop(Station &s, uint32_t *trigger_at) {
    if (s.q_count == 0) return false;
    *trigger_at = s.queue_trigger_time[s.q_head];
    s.q_head = (s.q_head + 1) % QUEUE_SIZE;
    s.q_count--;
    return true;
}

static bool queuePeek(const Station &s, uint32_t *trigger_at) {
    if (s.q_count == 0) return false;
    *trigger_at = s.queue_trigger_time[s.q_head];
    return true;
}

// =====================================================================
//                       HAM DIEU KHIEN SOLENOID
// =====================================================================
static void solenoidInit(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
}

static void solenoidSet(Station &s, bool on) {
    gpio_put(s.sol_pin, on ? 1 : 0);
    printf("[%s] Solenoid (GP%u) -> %s | d=%u mm | Tong: %lu\n",
           s.name, s.sol_pin, on ? "HUT (ON)" : "NHA (OFF)", s.distance_mm, (unsigned long)s.total_count);
}

// =====================================================================
//                       HAM CAM BIEN ToF (VL53L1X)
// =====================================================================
static void i2cBusInit(Station &s) {
    i2c_init(s.i2c, I2C_BAUDRATE);
    gpio_set_function(s.sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(s.scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(s.sda_pin);
    gpio_pull_up(s.scl_pin);
}

static bool tofInit(Station &s) {
    s.last_retry_ms = millis();
    const unsigned sda_idle = gpio_get(s.sda_pin);
    const unsigned scl_idle = gpio_get(s.scl_pin);
    s.sensor_initialized = s.tof->init(true);
    if (!s.sensor_initialized) {
        printf("[%s] LOI: Khong khoi tao duoc VL53L1X tren %s (SDA=GP%u, SCL=GP%u)\n",
               s.name, s.i2c == i2c0 ? "I2C0" : "I2C1", s.sda_pin, s.scl_pin);
        const auto &diag = s.tof->initDiagnostic();
        printf("[%s] TOF_DIAG stage=%s model=0x%04X boot=0x%02X SDA_IDLE=%u SCL_IDLE=%u",
               s.name, diag.stage, (unsigned)diag.model_id, (unsigned)diag.boot_status, sda_idle, scl_idle);
        if (diag.i2c_operation) {
            const char *reason = "SHORT_TRANSFER";
            if (diag.i2c_result == PICO_ERROR_GENERIC) reason = "NO_ACK_OR_BUS_ERROR";
            else if (diag.i2c_result == PICO_ERROR_TIMEOUT) reason = "I2C_TIMEOUT";
            printf(" i2c=%s op=%s reg=0x%04X rc=%d",
                   reason, diag.i2c_operation, (unsigned)diag.i2c_register, diag.i2c_result);
        }
        printf("\n");
        s.sensor_ok = false;
        return false;
    }
    s.tof->setDistanceMode(VL53L1X::Medium);       // Che do tam trung (chinh xac cho bang tai)
    s.tof->setMeasurementTimingBudget(40000);     // 40ms/lan do (~25Hz)
    s.tof->startContinuous(40);                   // Do lien tuc chu ky 40ms
    s.sensor_ok        = true;
    s.has_valid_target = false;
    s.hit_count        = 0;
    s.clear_count      = 0;
    s.last_data_ms     = millis();
    printf("[%s] VL53L1X khoi tao THANH CONG!\n", s.name);
    return true;
}

static bool tofRead(Station &s) {
    if (!s.sensor_ok) return false;

    if (s.tof->dataReady()) {
        uint16_t d = s.tof->read(false);
        uint8_t status = s.tof->ranging_data.range_status;

        // Status == 0 la ket qua do hop le va tin cay
        if (status == 0 && d > 0 && d < MAX_VALID_RANGE_MM) {
            s.distance_mm = d;
            s.has_valid_target = true;
        } else {
            // Khong co vat the hoac ngoai tam do
            s.distance_mm = MAX_VALID_RANGE_MM;
            s.has_valid_target = false;
        }

        s.last_data_ms = millis();
        return true;
    }

    if (millis() - s.last_data_ms > SENSOR_TIMEOUT_MS) {
        printf("[%s] LOI: Mat tin hieu cam bien!\n", s.name);
        s.sensor_ok = false;
    }
    return false;
}

// =====================================================================
//                       LOGIC MOI TRAM (STATE MACHINE)
// =====================================================================
static std::optional<uint16_t> stationUpdate(Station &s) {
    uint32_t now = millis();

    // 1. Kiem tra ket noi cam bien
    if (!s.sensor_ok) {
        if (s.state != ST_IDLE) {
            solenoidSet(s, false);
            s.state = ST_IDLE;
        }
        if (now - s.last_retry_ms > SENSOR_RETRY_MS) tofInit(s);
        return {};
    }

    // 2. Doc cam bien & loc nhieu (debounce)
    const bool fresh = tofRead(s);
    if (fresh) {
        bool matches_size = s.has_valid_target &&
                            s.distance_mm >= s.min_mm && s.distance_mm <= s.max_mm;
        if (matches_size) { if (s.hit_count < 255) s.hit_count++; }
        else       { s.hit_count = 0; }

        // Chi phep nhan phoi moi khi thay mat bang tai, khong dung mau loi de mo khoa.
        if (s.has_valid_target && s.distance_mm >= BELT_CLEAR_MIN_MM) {
            if (s.clear_count < 255) s.clear_count++;
        } else {
            s.clear_count = 0;
        }
    }

    bool object_detected = (s.hit_count >= DEBOUNCE_COUNT);
    bool object_cleared  = (s.clear_count >= CLEAR_COUNT);

    // 3. Phat hien vat moi -> dua vao hang doi (Queue)
    if (s.state == ST_IDLE && object_detected) {
        s.total_count++;
        printf("[%s] PHAT HIEN VAT #%lu: d=%u mm (size %u-%u mm)\n",
               s.name, (unsigned long)s.total_count, s.distance_mm, s.min_mm, s.max_mm);

        if (s.travel_delay_ms == 0) {
            solenoidSet(s, true);
            s.state = ST_ACTIVE;
            s.state_start_ms = now;
        } else {
            queuePush(s, now + s.travel_delay_ms);
            printf("[%s] Cho vat chay toi tay gat sau %lu ms...\n", s.name, (unsigned long)s.travel_delay_ms);
            s.state = ST_WAIT_CLEAR;
        }
    }

    // 4. Kiem tra hang doi tre di chuyen
    if (s.state != ST_ACTIVE && s.q_count > 0) {
        uint32_t target_time = 0;
        if (queuePeek(s, &target_time) && (int32_t)(now - target_time) >= 0) {
            queuePop(s, &target_time);
            solenoidSet(s, true);
            s.state = ST_ACTIVE;
            s.state_start_ms = now;
        }
    }

    // 5. Quan ly thoi gian Solenoid dang hut
    if (s.state == ST_ACTIVE) {
        uint32_t elapsed = now - s.state_start_ms;

        if (s.mode == MODE_PULSE) {
            if (elapsed >= s.on_time_ms) {
                solenoidSet(s, false);
                s.state = ST_WAIT_CLEAR;
            }
        } else { // MODE_HOLD
            if (object_cleared) {
                printf("[%s] Vat da qua tay gat sau %lu ms\n", s.name, (unsigned long)elapsed);
                solenoidSet(s, false);
                s.state = ST_IDLE;
                s.hit_count = 0;
            } else if (elapsed >= s.on_time_ms) {
                printf("[%s] CANH BAO: Hut qua %lu ms, nha de bao ve cuon day!\n",
                       s.name, (unsigned long)s.on_time_ms);
                solenoidSet(s, false);
                s.state = ST_WAIT_CLEAR;
            }
        }
    }

    // 6. Cho vat roi khoi vung do truoc khi nhan vat tiep theo
    if (s.state == ST_WAIT_CLEAR) {
        if (object_cleared) {
            s.state = ST_IDLE;
            s.hit_count = 0;
        }
    }
    if (!fresh) return {};
    return s.has_valid_target ? s.distance_mm : 0;
}

// =====================================================================
//                       DEBUG SERIAL
// =====================================================================
static const char *stateName(StationState st) {
    switch (st) {
    case ST_IDLE:       return "IDLE";
    case ST_ACTIVE:     return "ACTIVE";
    case ST_WAIT_CLEAR: return "WAIT_CLR";
    }
    return "?";
}

static const char *sensorErrorName(const Station &s) {
    const auto &diag = s.tof->initDiagnostic();
    if (diag.i2c_operation) {
        if (diag.i2c_result == PICO_ERROR_TIMEOUT) return "I2C_TIMEOUT";
        if (diag.i2c_result == PICO_ERROR_GENERIC) return "NO_ACK_OR_BUS_ERROR";
        return "SHORT_TRANSFER";
    }
    return s.sensor_initialized ? "MAT_DU_LIEU" : "LOI_KHOI_TAO";
}

static void printStatus(const std::optional<BeltSpeedMonitor> &monitor) {
    for (int i = 0; i < 2; i++) {
        Station &s = stations[i];
        if (!s.sensor_ok) {
            printf("%s: %s !          ", s.name, sensorErrorName(s));
        } else if (!s.has_valid_target || s.distance_mm >= MAX_VALID_RANGE_MM) {
            printf("%s: d= ----mm [%-8s | #%2lu] ", s.name, stateName(s.state), (unsigned long)s.total_count);
        } else {
            printf("%s: d=%4umm [%-8s | #%2lu] ", s.name, s.distance_mm, stateName(s.state), (unsigned long)s.total_count);
        }
    }
    if (monitor && monitor->speedMmPerSecond()) {
        printf("| v_mm_s=%.1f", *monitor->speedMmPerSecond());
        if (auto age = monitor->pairAgeMs()) printf(" | v_age_s=%.1f", *age / 1000.0);
    } else {
        printf("| v_mm_s=-- (%s)", monitor ? "cho cap ToF" : "cho cam bien");
    }
    printf("\n");
}

// =====================================================================
//                       MAIN
// =====================================================================
int main() {
    stdio_init_all();

    for (int i = 0; i < 30 && !stdio_usb_connected(); i++) sleep_ms(100);
    printf("\n=======================================================\n");
    printf("   BANG TAI PHAN LOAI TU DONG - Raspberry Pi Pico 2 W   \n");
    printf("=======================================================\n");

    bool led_ok = (cyw43_arch_init() == 0);

    for (int i = 0; i < 2; i++) {
        Station &s = stations[i];
        s.state = ST_IDLE;
        s.distance_mm = MAX_VALID_RANGE_MM;
        s.has_valid_target = false;
        s.total_count = 0;
        s.q_head = s.q_tail = s.q_count = 0;
        solenoidInit(s.sol_pin);
        i2cBusInit(s);
        tofInit(s);
    }

    uint32_t last_print = 0, last_blink = 0;
    bool led_state = false;
    std::optional<BeltSpeedMonitor> speed;

    while (true) {
        for (int i = 0; i < 2; ++i) {
            Station &s = stations[i];
            const auto sample = stationUpdate(s);
            if (!stations[0].sensor_ok || !stations[1].sensor_ok) {
                speed.reset(); // Never match across a sensor outage.
                continue;
            }
            if (!speed) speed.emplace(speed_config);
            if (sample) {
                const auto event = speed->update(s.last_data_ms, i == 0 ? sample : std::nullopt,
                                                 i == 1 ? sample : std::nullopt);
                if (event.pair_mm_s) {
                    printf("[VAN TOC] Phoi %c | v_pair=%.1f mm/s\n",
                           event.station2->size == PartSize::L ? 'L' : 'S', *event.pair_mm_s);
                }
            }
        }

        uint32_t now = millis();
        if (speed) speed->update(now);
        if (now - last_print >= PRINT_PERIOD_MS) {
            last_print = now;
            printStatus(speed);
        }
        if (led_ok && now - last_blink >= 500) {
            last_blink = now;
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        }
    }
}
