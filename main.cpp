/**
 * =====================================================================
 *  BANG TAI PHAN LOAI TU DONG - Raspberry Pi Pico 2 W (VL53L1X)
 * =====================================================================
 *  Phan cung:
 *    - ToF 1 (VL53L1X) : I2C0  SDA=GP0, SCL=GP1, LPN=3V3 -> Tram 1
 *    - ToF 2 (VL53L1X) : I2C1  SDA=GP2, SCL=GP3, LPN=3V3 -> Tram 2
 *    - Solenoid 1      : GP10 (HIGH = hut, LOW = nha) qua L298N IN1
 *    - Solenoid 2      : GP11 (HIGH = hut, LOW = nha) qua L298N IN3
 *
 *  Logic:
 *    - 2 tram chay DOC LAP va DONG THOI, khong chan CPU (Non-blocking).
 *    - Tram 1 (MODE_PULSE): vat < 150mm -> hut Solenoid 1 trong 2s roi nha.
 *    - Tram 2 (MODE_HOLD) : vat < 200mm -> hut Solenoid 2 cho den khi vat qua.
 *    - Co bo dem san pham phan loai, loc nhieu (debounce), chong hut lap.
 * =====================================================================
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"
#include "vl53l1x.h"

// ======================= CAU HINH CHAN =======================
#define TOF1_I2C        i2c0
#define TOF1_SDA_PIN    0
#define TOF1_SCL_PIN    1

#define TOF2_I2C        i2c1
#define TOF2_SDA_PIN    2
#define TOF2_SCL_PIN    3

#define SOL1_PIN        10
#define SOL2_PIN        11

#define I2C_BAUDRATE    (400 * 1000)   // 400 kHz

// ======================= THAM SO HE THONG =======================
#define HYSTERESIS_MM       20     // Do tre (mm) chong rung Solenoid
#define DEBOUNCE_COUNT      2      // So mau lien tiep < nguong de xac nhan co vat
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
    uint16_t      threshold_mm;    // Nguong phat hien vat (mm)
    uint32_t      travel_delay_ms; // Thoi gian vat chay tu cam bien -> tay gat (0 = gat ngay)
    uint32_t      on_time_ms;      // Thoi gian Solenoid hut (ms)

    // --- Trang thai runtime ---
    VL53L1X      *tof;
    bool          sensor_ok;
    bool          has_valid_target;
    uint16_t      distance_mm;
    uint32_t      last_data_ms;
    uint32_t      last_retry_ms;
    uint8_t       hit_count;
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
// Trạm 1: Ngưỡng < 150mm (15cm), trễ di chuyển 0ms, hút 2000ms rồi nhả (MODE_PULSE)
// Trạm 2: Ngưỡng < 200mm (20cm), trễ di chuyển 0ms, hút đến khi vật qua / max 5000ms (MODE_HOLD)
static Station stations[2] = {
    { "TRAM1", TOF1_I2C, TOF1_SDA_PIN, TOF1_SCL_PIN, SOL1_PIN, MODE_PULSE, 150, 0, 2000, &tof1 },
    { "TRAM2", TOF2_I2C, TOF2_SDA_PIN, TOF2_SCL_PIN, SOL2_PIN, MODE_HOLD,  200, 0, 5000, &tof2 },
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
    if (!s.tof->init(true)) {
        printf("[%s] LOI: Khong khoi tao duoc VL53L1X tren %s (SDA=GP%u, SCL=GP%u)\n",
               s.name, s.i2c == i2c0 ? "I2C0" : "I2C1", s.sda_pin, s.scl_pin);
        s.sensor_ok = false;
        return false;
    }
    s.tof->setDistanceMode(VL53L1X::Medium);       // Che do tam trung (chinh xac cho bang tai)
    s.tof->setMeasurementTimingBudget(40000);     // 40ms/lan do (~25Hz)
    s.tof->startContinuous(40);                   // Do lien tuc chu ky 40ms
    s.sensor_ok        = true;
    s.has_valid_target = false;
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
static void stationUpdate(Station &s) {
    uint32_t now = millis();

    // 1. Kiem tra ket noi cam bien
    if (!s.sensor_ok) {
        if (s.state != ST_IDLE) {
            solenoidSet(s, false);
            s.state = ST_IDLE;
        }
        if (now - s.last_retry_ms > SENSOR_RETRY_MS) tofInit(s);
        return;
    }

    // 2. Doc cam bien & loc nhieu (debounce)
    if (tofRead(s)) {
        bool below = s.has_valid_target && (s.distance_mm < s.threshold_mm);
        if (below) { if (s.hit_count < 255) s.hit_count++; }
        else       { s.hit_count = 0; }
    }

    bool object_detected = (s.hit_count >= DEBOUNCE_COUNT);
    bool object_cleared  = !s.has_valid_target || (s.distance_mm >= s.threshold_mm + HYSTERESIS_MM);

    // 3. Phat hien vat moi -> dua vao hang doi (Queue)
    if (s.state == ST_IDLE && object_detected) {
        s.total_count++;
        printf("[%s] PHAT HIEN VAT #%lu: d=%u mm (< %u mm)\n",
               s.name, (unsigned long)s.total_count, s.distance_mm, s.threshold_mm);

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

static void printStatus() {
    for (int i = 0; i < 2; i++) {
        Station &s = stations[i];
        if (!s.sensor_ok) {
            printf("%s: LOI CAM BIEN              ", s.name);
        } else if (!s.has_valid_target || s.distance_mm >= MAX_VALID_RANGE_MM) {
            printf("%s: d= ----mm [%-8s | #%2lu] ", s.name, stateName(s.state), (unsigned long)s.total_count);
        } else {
            printf("%s: d=%4umm [%-8s | #%2lu] ", s.name, s.distance_mm, stateName(s.state), (unsigned long)s.total_count);
        }
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

    while (true) {
        stationUpdate(stations[0]);
        stationUpdate(stations[1]);

        uint32_t now = millis();
        if (now - last_print >= PRINT_PERIOD_MS) {
            last_print = now;
            printStatus();
        }
        if (led_ok && now - last_blink >= 500) {
            last_blink = now;
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        }
    }
}
