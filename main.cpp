/**
 * =====================================================================
 *  BANG TAI PHAN LOAI TU DONG - Raspberry Pi Pico 2 W (VL53L1X)
 * =====================================================================
 *  Phan cung:
 *    - ToF 1 (VL53L1X) : I2C0  SDA=GP0, SCL=GP1, LPN=3V3 -> Tram 1
 *    - ToF 2 (VL53L1X) : I2C1  SDA=GP6, SCL=GP7, LPN=3V3 -> Tram 2
 *    - Servo SG90 T1   : GP10 PWM 50 Hz (nguon 5V rieng, GND chung) - thay Solenoid 1
 *    - LM393 T1 (DO)   : GP12 - xac nhan phoi toi vi tri day (cuc tinh: LM393_ACTIVE_LOW)
 *    - Solenoid 2      : GP11 (HIGH = hut, LOW = nha) qua L298N IN3
 *
 *  Logic:
 *    - 2 tram chay DOC LAP va DONG THOI, khong chan CPU (Non-blocking).
 *    - Tram 1: size L (mac dinh cao 22-100mm) -> ToF xac dinh L, cho LM393 xac nhan vi tri
 *      (chong nhieu), SG90 day SERVO_PUSH_MS roi ve nghi. Timeout/mat ToF/tat he thong -> ve nghi.
 *    - Tram 2: size M (mac dinh cao 7-20mm) -> kich Solenoid 2 roi nha.
 *    - Size S va vung dem giua hai khoang di thang qua ca hai tram (khong day).
 *    - Nguong va thoi gian gat co the chinh truc tiep tu Web Dashboard.
 *    - L298N: IN2/IN4 noi GND, ENA/ENB bat, GND chung voi Pico.
 *    - Co bo dem san pham phan loai, loc nhieu (debounce), chong hut lap.
 * =====================================================================
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pegatron_monitor.h"
#include "pegatron_local_config.h"
#include "vl53l1x.h"
#include "station1_servo_trial_logic.h"

#ifndef USE_SORTER_ENGINE
#define USE_SORTER_ENGINE 1
#endif

#if USE_SORTER_ENGINE
#include "sorter_fsm.h"
#include "calib_math.h"
#endif

// ======================= CAU HINH CHAN =======================
#define TOF1_I2C        i2c0
#define TOF1_SDA_PIN    0
#define TOF1_SCL_PIN    1

#define TOF2_I2C        i2c1
#define TOF2_SDA_PIN    6
#define TOF2_SCL_PIN    7

#define SOL1_PIN        10     // Tram 1: chan tin hieu PWM cua SG90 (KHONG con la solenoid)
#define SOL2_PIN        11
#define LM393_PIN       12     // Tram 1: DO cua LM393 xac nhan phoi o vi tri day

#define I2C_BAUDRATE    (100 * 1000)   // 100 kHz, da thu voi hai cam bien

// ===== SG90 + LM393 TRAM 1 (gia tri khoi dau, PHAI hieu chinh tren ban, xem README/bao cao) =====
#define LM393_ACTIVE_LOW    1      // 1: co vat -> DO=0. Do bang dong ho roi doi 0/1 neu nguoc.
#define SERVO_HOME_US       1500   // Xung nghi (us). Chinh khi can day KHONG cham bang tai.
#define SERVO_PUSH_US       2500   // Xung day (us). Tang tung 50us, KHONG vuot hanh trinh co khi.
#define SERVO_MIN_US        1000   // Chan an toan: khong bao gio xuat xung ngoai khoang nay
#define SERVO_MAX_US        2500   // SG90 toi da ~2500us; kiem tra co khi truoc khi tang PUSH
#define SERVO_PUSH_MS       160    // Giu o vi tri day (SG90 ~150ms/90 do o 5V)
#define SERVO_RECOVER_MS    150    // Cho servo thu ve het hanh trinh
#define LM393_CLEAR_MS      30     // Phoi da xu ly phai roi LM393 >= ms nay truoc khi nhan phoi tiep
#define LM393_STABLE_MS     10     // LM393 phai on dinh ACTIVE >= ms nay (chong nhieu)
#define LM393_WAIT_LIMIT_MS 1500   // Ket qua ToF trong hang doi qua ms nay chua toi LM393 -> bo
static_assert(SERVO_HOME_US >= SERVO_MIN_US && SERVO_HOME_US <= SERVO_MAX_US, "HOME ngoai khoang an toan");
static_assert(SERVO_PUSH_US >= SERVO_MIN_US && SERVO_PUSH_US <= SERVO_MAX_US, "PUSH ngoai khoang an toan");

// ======================= THAM SO HE THONG =======================
#define BELT_CLEAR_MIN_MM   80     // Mat bang tai 85-90mm; >= 80mm moi cho nhan phoi tiep
#define DEBOUNCE_COUNT      2      // So mau lien tiep trong khoang size de xac nhan phoi
#define CLEAR_COUNT         1      // So mau lien tiep thay mat bang tai de chong kich lap
#define SENSOR_RETRY_MS     2500   // Thu khoi tao lai cam bien neu mat ket noi
#define SENSOR_TIMEOUT_MS   800    // Timeout tin hieu cam bien
#define PRINT_PERIOD_MS     250    // Chu ky in log ra Serial Monitor
#define QUEUE_SIZE          8      // So luong vat toi da trong hang doi tre di chuyen
#define MAX_VALID_RANGE_MM  4000   // Khoang cach do toi da hop le cua VL53L1X (4m)

#if !USE_SORTER_ENGINE
#error "Tram 1 dung SG90 + LM393, can USE_SORTER_ENGINE=1 (tranh 2 co che cung dieu khien GP10)"
#endif
#if USE_SORTER_ENGINE
#define DEFAULT_H0_MM       82.0   // H0 ToF tram 1/2 khi chua co PEGATRON_REFERENCE_x_MM; DO lai tren ban
#define DETECT_DELTA_MM     6.0    // Nguong phat hien vat thap hon mat bang (mm)
#define CLEAR_DELTA_MM      5.0    // Tre hysteresis mo khoa WaitClear (mm)
#define THRESHOLD_SM_MM     7.0    // Nguong T_SM (<=7mm: S, >7mm: M)
#define THRESHOLD_ML_MM     20.0   // Nguong T_ML (>20mm: L)
#define L_HEIGHT_MIN_MM     28.0   // L that 40mm, ToF do ~33 (thap ~7mm). h>=28 la L (Tram 1)
#define L_HEIGHT_MAX_MM     100.0  // Chieu cao toi da size L (Tram 1)
#define M_HEIGHT_MIN_MM     18.0   // M that 30mm, ToF do ~22. 18<=h<28 la M (Tram 2); S do ~13-17
#define M_HEIGHT_MAX_MM     28.0   // L duoc xet truoc nen h>=28 la L
#define MEDIAN_SAMPLES      3      // So mau trung vi sau khi bo mau mep
#define SOLENOID_MAX_ON_MS  600    // Chan tren thoi gian hut solenoid (ms)
#define SOLENOID_MIN_OFF_MS 120    // Thoi gian hoi toi thieu giua hai nhip gat (ms)
#define SOLENOID_RATED_DUTY 0.25   // Gioi han nhiet trong cua so 60s
#endif

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
    uint32_t      push_count;
#if USE_SORTER_ENGINE
    sorter_core::StationConfig    detector_cfg{};
    sorter_core::TriggerGeometry  geometry{};
    sorter_core::ItemDetector     detector{sorter_core::StationConfig{}};
    sorter_core::SolenoidActuator actuator{};
    uint32_t                      missed_count = 0;
    bool                          manual_test_active = false;
    bool                          servo = false;     // true: tram dung SG90 + LM393 thay solenoid
    Station1TrialGate             servo_gate{};
    uint16_t                      servo_us = 0;
    uint16_t                      raw_mm = 0;          // chan doan: mau ToF tho cuoi cung
    uint8_t                       raw_status = 255;    // chan doan: range_status cuoi cung
    uint32_t                      bad_samples = 0;     // chan doan: so mau khong hop le
#endif
};

// Web dashboard can pause sorting (belt keeps being measured, solenoids stay off).
static bool sorting_enabled = true;
#if USE_SORTER_ENGINE
static sorter_core::SystemFsm system_fsm;
static uint32_t tray_s_count = 0;
#endif

static VL53L1X tof1(TOF1_I2C);
static VL53L1X tof2(TOF2_I2C);

// Cau hinh 2 tram (D-13: thu tu L -> M -> S khong day):
// Tram 1: size L (<62mm mặc định), kích đẩy L.
// Tram 2: size M (62-75mm mặc định), kích đẩy M.
// Size S nam ngoai hai khoang nay nen di thang qua ca hai tram.
static Station stations[2] = {
    { "TRAM1", TOF1_I2C, TOF1_SDA_PIN, TOF1_SCL_PIN, SOL1_PIN, MODE_PULSE, 0, 62, 0, 220, &tof1 },
    { "TRAM2", TOF2_I2C, TOF2_SDA_PIN, TOF2_SCL_PIN, SOL2_PIN, MODE_PULSE, 62, 75, 0, 220, &tof2 },
};

// ======================= TIEN ICH =======================
static inline uint32_t millis() {
    return to_ms_since_boot(get_absolute_time());
}

#if !USE_SORTER_ENGINE
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
#endif

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
    if (on) ++s.push_count;
    printf("[%s] Solenoid (GP%u) -> %s | d=%u mm | Tong: %lu\n",
           s.name, s.sol_pin, on ? "HUT (ON)" : "NHA (OFF)", s.distance_mm, (unsigned long)s.total_count);
}

// ---------------------- SG90 + LM393 (tram 1) ----------------------
static void servoWrite(Station &s, uint16_t us) {
    if (us < SERVO_MIN_US) us = SERVO_MIN_US;
    if (us > SERVO_MAX_US) us = SERVO_MAX_US;
    if (us == s.servo_us) return;
    pwm_set_gpio_level(s.sol_pin, us);   // 1 MHz counter: level = pulse width in us
    s.servo_us = us;
}

static void servoInit(Station &s) {
    gpio_set_function(s.sol_pin, GPIO_FUNC_PWM);
    const uint slice = pwm_gpio_to_slice_num(s.sol_pin);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, static_cast<float>(clock_get_hz(clk_sys)) / 1000000.0f);
    pwm_config_set_wrap(&config, 19999);  // 20 ms period
    pwm_init(slice, &config, false);
    pwm_set_gpio_level(s.sol_pin, SERVO_HOME_US);
    pwm_set_enabled(slice, true);
    s.servo_us = SERVO_HOME_US;
    s.servo_gate = Station1TrialGate{};
    s.servo_gate.position_stable_ms = LM393_STABLE_MS;
    s.servo_gate.wait_limit_ms = LM393_WAIT_LIMIT_MS;
    s.servo_gate.push_ms = SERVO_PUSH_MS;
    s.servo_gate.recover_ms = SERVO_RECOVER_MS;
    s.servo_gate.clear_ms = LM393_CLEAR_MS;
    gpio_init(LM393_PIN);
    gpio_set_dir(LM393_PIN, GPIO_IN);
    gpio_pull_up(LM393_PIN);
}

static bool lm393Raw() { return gpio_get(LM393_PIN); }
static bool lm393Active() { return lm393Raw() == (LM393_ACTIVE_LOW ? false : true); }

// Back to HOME and forget any pending item (pause, fault, E-stop, calib, sensor loss).
static void servoAbort(Station &s) {
    s.servo_gate.reset();
    servoWrite(s, SERVO_HOME_US);
}

static void servoStep(Station &s, uint32_t now, bool enabled) {
    static bool last_raw = lm393Raw();  // log moi lan LM393 doi muc de kiem tra cam bien
    if (lm393Raw() != last_raw) {
        last_raw = lm393Raw();
        printf("[LM393] DO=%u -> %s (t=%lu ms)\n", (unsigned)last_raw,
               lm393Active() ? "CO VAT" : "KHONG VAT", (unsigned long)now);
    }
    switch (s.servo_gate.tick(now, enabled, s.sensor_ok, lm393Active())) {
    case TrialEvent::Passed:       printf("[%s] LM393 thay phoi M/S -> cho qua (hang doi con %u)\n", s.name, (unsigned)s.servo_gate.q_count); break;
    case TrialEvent::Unmatched:    ++s.missed_count; printf("[%s] CANH BAO: LM393 thay phoi nhung ToF chua do -> KHONG DAY\n", s.name); break;
    case TrialEvent::Push:        ++s.push_count; printf("[%s] SERVO DAY (%u us) | Tong day: %lu\n", s.name, (unsigned)SERVO_PUSH_US, (unsigned long)s.push_count); break;
    case TrialEvent::Returned:     printf("[%s] SERVO THU VE, cho vat roi LM393\n", s.name); break;
    case TrialEvent::Ready:        printf("[%s] SAN SANG cho phoi tiep theo\n", s.name); break;
    case TrialEvent::TimedOut:     ++s.missed_count; printf("[%s] CANH BAO: LM393 khong xac nhan trong %u ms -> BO PHOI\n", s.name, (unsigned)LM393_WAIT_LIMIT_MS); break;
    case TrialEvent::SensorLost:   printf("[%s] MAT ToF -> servo ve nghi, khong day\n", s.name); break;
    default: break;
    }
    servoWrite(s, s.servo_gate.stage == TrialStage::Extended ? SERVO_PUSH_US : SERVO_HOME_US);
}

// Stop whatever the station is doing (solenoid release or servo home).
static void stationRelease(Station &s, uint32_t now) {
    if (s.servo) { servoAbort(s); return; }
    const auto off_tick = s.actuator.tick(now, false);
    if (off_tick.changed) solenoidSet(s, false);
}

static bool stationActive(const Station &s) {
    return s.servo ? s.servo_gate.stage == TrialStage::Extended : s.actuator.is_on();
}

// Bench push. Caller has already checked FSM is Idle and sensor_ok.
static void stationManualTest(Station &s, uint32_t now) {
    if (s.servo) {
        if (!s.servo_gate.force_push(now)) return;
        s.manual_test_active = true;
        ++s.push_count;
        servoWrite(s, SERVO_PUSH_US);
        printf("[%s] TEST: servo day (%u us)\n", s.name, (unsigned)SERVO_PUSH_US);
    } else if (s.actuator.trigger_manual(now, s.on_time_ms)) {
        s.manual_test_active = true;
        const auto on_tick = s.actuator.tick(now, true);
        if (on_tick.changed && on_tick.solenoid_on) solenoidSet(s, true);
    }
    s.state = stationActive(s) ? ST_ACTIVE : ST_IDLE;
}

// =====================================================================
//                       HAM CAM BIEN ToF (VL53L1X)
// =====================================================================
static void i2cBusRecover(uint sda_pin, uint scl_pin) {
    gpio_init(sda_pin);
    gpio_set_dir(sda_pin, GPIO_IN);
    gpio_pull_up(sda_pin);

    gpio_init(scl_pin);
    gpio_set_dir(scl_pin, GPIO_OUT);
    gpio_put(scl_pin, 1);
    sleep_us(5);

    if (!gpio_get(sda_pin)) {
        for (int pulse = 0; pulse < 9 && !gpio_get(sda_pin); ++pulse) {
            gpio_put(scl_pin, 0);
            sleep_us(5);
            gpio_put(scl_pin, 1);
            sleep_us(5);
        }
        // Phat STOP condition de nha slave bi treo sau soft reset.
        gpio_set_dir(sda_pin, GPIO_OUT);
        gpio_put(sda_pin, 0);
        sleep_us(5);
        gpio_put(scl_pin, 1);
        sleep_us(5);
        gpio_set_dir(sda_pin, GPIO_IN);
        gpio_pull_up(sda_pin);
        sleep_us(5);
    }
}

static void i2cBusInit(Station &s) {
    i2cBusRecover(s.sda_pin, s.scl_pin);
    i2c_init(s.i2c, I2C_BAUDRATE);
    gpio_set_function(s.sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(s.scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(s.sda_pin);
    gpio_pull_up(s.scl_pin);
}

static bool tofInit(Station &s) {
    s.last_retry_ms = millis();
    i2cBusInit(s);
    sleep_us(10);
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
    s.tof->setMeasurementTimingBudget(15000);     // 15ms/lan do (~66Hz) bat kip 2 vat gan nhau
    s.tof->startContinuous(15);                   // Do lien tuc chu ky 15ms
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
        s.raw_mm = d;
        s.raw_status = status;
        if (!(status == 0 && d > 0 && d < MAX_VALID_RANGE_MM)) ++s.bad_samples;

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
#if USE_SORTER_ENGINE
static const char *sizeName(sorter_core::Size sz) {
    switch (sz) {
    case sorter_core::Size::S: return "S";
    case sorter_core::Size::M: return "M";
    case sorter_core::Size::L: return "L";
    default: return "?";
    }
}

static void stationUpdate(Station &s) {
    const uint32_t now = millis();

    // 1. Kiem tra ket noi cam bien
    if (!s.sensor_ok) {
        if (s.servo) servoStep(s, now, true);  // logs SensorLost, servo -> HOME
        stationRelease(s, now);
        s.manual_test_active = false;
        s.detector.reset();
        s.state = ST_IDLE;
        if (now - s.last_retry_ms > SENSOR_RETRY_MS) tofInit(s);
        return;
    }

    // 2. Doc cam bien & chay ItemDetector (bo mau mep + trung vi N mau + D-11)
    if (tofRead(s)) {
        const double sample_mm =
            s.has_valid_target ? static_cast<double>(s.distance_mm) : -1.0;
        const sorter_core::DetectionResult res = s.detector.feed(sample_mm, now);
        // Chan doan: moi mau ToF khi co vat (d, status, h tam tinh = H0 - d, trang thai detector).
        // det: 0=Idle 1=Collecting(dang gom mau) 2=WaitClear(da chot, cho vat roi di)
        if (s.detector.state() != sorter_core::ItemDetector::State::Idle || res.ready) {
            printf("[%s] MAU d=%u st=%u h~%.1f det=%u%s\n", s.name, (unsigned)s.raw_mm,
                   (unsigned)s.raw_status, s.detector_cfg.h0_mm - s.raw_mm,
                   (unsigned)s.detector.state(), res.ready ? " <-CHOT" : "");
        }
        if (res.ready) {
            printf("[%s] KET QUA: d_med=%.1f h=%.1f H0=%.1f L=[%.0f,%.0f] M=[%.0f,%.0f] -> size=%s action=%s%s\n",
                   s.name, res.median_distance_mm, res.height_mm, s.detector_cfg.h0_mm,
                   s.detector_cfg.l_min_mm, s.detector_cfg.l_max_mm,
                   s.detector_cfg.m_min_mm, s.detector_cfg.m_max_mm, sizeName(res.size),
                   res.action == sorter_core::Action::Push ? "PUSH" :
                   res.action == sorter_core::Action::Missed ? "MISSED" : "PASS",
                   system_fsm.state() == sorter_core::SystemState::Running ? "" : " (KHONG CHAY: chi log)");
        }
        if (res.ready && system_fsm.state() == sorter_core::SystemState::Running) {
            if (res.action == sorter_core::Action::Push) {
                const uint32_t td = sorter_core::compute_trigger_delay_ms(s.geometry);
                if (s.servo) {
                    if (!s.servo_gate.enqueue(true, now))
                        printf("[%s] CANH BAO: Hang doi servo day!\n", s.name);
                } else if (!s.actuator.enqueue(res.t0_ms, td)) {
                    printf("[%s] CANH BAO: Hang doi hen gio day!\n", s.name);
                }
                ++s.total_count;
                printf("[%s] PHAT HIEN #%lu: d_med=%.1f h=%.1fmm size=%s -> PUSH (td=%lu ms)\n",
                       s.name, (unsigned long)s.total_count, res.median_distance_mm,
                       res.height_mm, sizeName(res.size), (unsigned long)td);
            } else if (res.action == sorter_core::Action::Missed) {
                ++s.missed_count;
                printf("[%s] CANH BAO: L lot qua Tram 1 (h=%.1fmm) -> MISSED (khong kich Solenoid)\n",
                       s.name, res.height_mm);
            } else if (res.action == sorter_core::Action::Pass) {
                if (s.servo && !s.servo_gate.enqueue(false, now))  // giu thu tu: M/S cung chiem 1 cho
                    printf("[%s] CANH BAO: Hang doi servo day!\n", s.name);
                if (s.detector_cfg.station == 2 && res.size == sorter_core::Size::S) {
                    ++tray_s_count;
                }
                printf("[%s] QUA TRAM: h=%.1fmm size=%s -> PASS\n",
                       s.name, res.height_mm, sizeName(res.size));
            }
        }
    }

    // 3. Cap nhat actuator FIFO + chan nhiet duty cycle
    const bool allow_actuation =
        system_fsm.outputs_allowed() ||
        (system_fsm.state() == sorter_core::SystemState::Idle && s.manual_test_active);
    if (s.servo) {
        servoStep(s, now, allow_actuation);
        if (s.servo_gate.stage == TrialStage::Idle) s.manual_test_active = false;
    } else {
    const sorter_core::ActuatorTick tick = s.actuator.tick(now, allow_actuation);
    if (tick.changed) solenoidSet(s, tick.solenoid_on);
    if (!tick.solenoid_on) s.manual_test_active = false;
    if (tick.overheat_fault) {
        system_fsm.trigger_fault(sorter_core::FaultCode::Overheat);
        sorting_enabled = false;
        printf("[%s] LOI QUA NHIET SOLENOID (duty=%.0f%%) -> FAULT!\n",
               s.name, tick.duty_ratio * 100.0);
    }
    }

    // 4. Dong bo trang thai hien thi Serial / Dashboard
    if (stationActive(s)) {
        s.state = ST_ACTIVE;
    } else if (s.detector.state() == sorter_core::ItemDetector::State::WaitClear) {
        s.state = ST_WAIT_CLEAR;
    } else {
        s.state = ST_IDLE;
    }
}
#else
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
    if (sorting_enabled && s.state == ST_IDLE && object_detected) {
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
}
#endif

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

static void printStatus() {
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
    printf("\n");
}

// =====================================================================
//                       MAIN
// =====================================================================
// Only core 0 reads the stations or changes an output. Core 1 owns CYW43/lwIP.
static queue_t snapshot_queue, command_queue, ack_queue;
alignas(32) static uint32_t network_stack[2048];

// One rule set for both the permissions shown on the web and the checks before acting.
static const char *denyReason(pegatron_action action, unsigned station) {
#if USE_SORTER_ENGINE
    switch (action) {
    case PEGATRON_ENABLE:
        if (system_fsm.state() == sorter_core::SystemState::Running) return "ALREADY_SORTING";
        if (system_fsm.estop_active() || system_fsm.state() == sorter_core::SystemState::Estop) {
            return "ESTOP_ACTIVE";
        }
        return nullptr;
    case PEGATRON_PAUSE:
        return system_fsm.state() == sorter_core::SystemState::Running ? nullptr : "ALREADY_PAUSED";
    case PEGATRON_TEST: {
        const Station &s = stations[station - 1];
        if (system_fsm.state() == sorter_core::SystemState::Running) return "SORTING_ACTIVE";
        if (system_fsm.estop_active() || system_fsm.state() == sorter_core::SystemState::Estop) {
            return "ESTOP_ACTIVE";
        }
        if (system_fsm.state() == sorter_core::SystemState::Fault) return "FAULT_ACTIVE";
        if (!s.sensor_ok) return "TOF_UNAVAILABLE";
        return s.state == ST_IDLE ? nullptr : "STATION_BUSY";
    }
    default:                 return "NOT_SUPPORTED";
    }
#else
    switch (action) {
    case PEGATRON_ENABLE:    return sorting_enabled ? "ALREADY_SORTING" : nullptr;
    case PEGATRON_PAUSE:     return sorting_enabled ? nullptr : "ALREADY_PAUSED";
    case PEGATRON_TEST: {
        const Station &s = stations[station - 1];
        if (sorting_enabled) return "SORTING_ACTIVE";
        if (!s.sensor_ok) return "TOF_UNAVAILABLE";
        return s.state == ST_IDLE ? nullptr : "STATION_BUSY";
    }
    default:                 return "NOT_SUPPORTED";
    }
#endif
}

static void answerCommand(const pegatron_command &command, const char *reason) {
    pegatron_ack ack{};
    snprintf(ack.request_id, sizeof ack.request_id, "%s", command.request_id);
    snprintf(ack.reason, sizeof ack.reason, "%s", reason ? reason : "OK");
    ack.executed = reason == nullptr;
    ack.mode = sorting_enabled ? PEGATRON_SORTING : PEGATRON_IDLE;  // Read back after acting.
    queue_try_add(&ack_queue, &ack);
}

static void runCommands() {
    pegatron_command command;
    while (queue_try_remove(&command_queue, &command)) {
        const char *reason = denyReason(command.action, command.station);
        if (!reason) {
            const uint32_t now = millis();
#if USE_SORTER_ENGINE
            if (command.action == PEGATRON_ENABLE) {
                if (system_fsm.state() == sorter_core::SystemState::Fault) {
                    system_fsm.request_reset(false);
                }
                system_fsm.request_start();
                sorting_enabled = (system_fsm.state() == sorter_core::SystemState::Running);
            } else if (command.action == PEGATRON_PAUSE) {
                system_fsm.request_stop();
                sorting_enabled = false;
                for (Station &s : stations) {  // Release at once, drop pending delayed pushes.
                    s.manual_test_active = false;
                    stationRelease(s, now);
                    s.detector.reset();
                    s.state = ST_IDLE;
                }
            } else {  // PEGATRON_TEST: one normal pulse; the state machine ends it.
                Station &s = stations[command.station - 1];
                stationManualTest(s, now);
            }
#else
            if (command.action == PEGATRON_ENABLE) {
                sorting_enabled = true;
            } else if (command.action == PEGATRON_PAUSE) {
                sorting_enabled = false;
                for (Station &s : stations) {  // Release at once, drop pending delayed pushes.
                    s.q_count = s.q_head = s.q_tail = 0;
                    if (s.state == ST_ACTIVE) {
                        solenoidSet(s, false);
                        s.state = ST_WAIT_CLEAR;
                    }
                }
            } else {  // PEGATRON_TEST: one normal pulse; the state machine ends it.
                Station &s = stations[command.station - 1];
                solenoidSet(s, true);
                s.state = ST_ACTIVE;
                s.state_start_ms = now;
            }
#endif
        }
        answerCommand(command, reason);
    }
}

static pegatron_snapshot makeSnapshot() {
    const int references[] = {PEGATRON_REFERENCE_1_MM, PEGATRON_REFERENCE_2_MM};
    pegatron_snapshot snapshot{};
#if USE_SORTER_ENGINE
    const auto fsm_state = system_fsm.state();
    snapshot.mode = (fsm_state == sorter_core::SystemState::Fault ||
                     fsm_state == sorter_core::SystemState::Estop)
                        ? PEGATRON_FAULT
                        : (sorting_enabled ? PEGATRON_SORTING : PEGATRON_IDLE);
#else
    snapshot.mode = sorting_enabled ? PEGATRON_SORTING : PEGATRON_IDLE;
#endif
    snapshot.deny[0] = denyReason(PEGATRON_ENABLE, 0);
    snapshot.deny[1] = denyReason(PEGATRON_PAUSE, 0);
    snapshot.deny[2] = denyReason(PEGATRON_TEST, 1);
    snapshot.deny[3] = denyReason(PEGATRON_TEST, 2);
    snapshot.deny[4] = snapshot.deny[5] = denyReason(PEGATRON_CALIBRATE, 0);
    for (unsigned i = 0; i < 2; ++i) {
        const Station &source = stations[i];
        pegatron_station &target = snapshot.station[i];
        const bool valid = source.sensor_ok && source.has_valid_target;
#if USE_SORTER_ENGINE
        const int ref = source.detector_cfg.h0_mm > 0.0
                            ? static_cast<int>(source.detector_cfg.h0_mm + 0.5)
                            : references[i];
#else
        const int ref = references[i];
#endif
        const int height = ref - source.distance_mm;
        target.sample_valid = target.distance_known = valid;
        target.distance_mm = source.distance_mm;
        target.push_count = source.push_count;
        target.reference_known = ref > 0;
        target.reference_mm = ref;
        target.height_known = valid && target.reference_known && height >= -2000 && height <= 2000;
        target.height_mm = target.height_known ? height : 0;
        snapshot.tof_unavailable[i] = !source.sensor_ok;
    }
#if USE_SORTER_ENGINE
    snapshot.tray_confirmed[0] = true;
    snapshot.tray_confirmed[1] = true;
    snapshot.tray_confirmed[2] = true;
    snapshot.tray_count[0] = tray_s_count;
    snapshot.tray_count[1] = stations[1].total_count;
    snapshot.tray_count[2] = stations[0].total_count;
#endif
    return snapshot;
}

static void networkWorker() {
    const pegatron_settings settings = {
        "pico-01", PEGATRON_WIFI_SSID, PEGATRON_WIFI_PASSWORD, PEGATRON_BROKER_IPV4,
        "pico-01", PEGATRON_MQTT_PASSWORD, 1883, &command_queue, &ack_queue
    };
    if (!pegatron_monitor_init(&settings)) {
        while (true) sleep_ms(1000);
    }
    pegatron_snapshot snapshot{};
    pegatron_snapshot next{};
    bool have_snapshot = false;
    uint32_t last_blink = 0;
    bool led_state = false;
    while (true) {
        bool changed = false;
        while (queue_try_remove(&snapshot_queue, &next)) {
            changed |= !have_snapshot || next.mode != snapshot.mode ||
                next.tof_unavailable[0] != snapshot.tof_unavailable[0] ||
                next.tof_unavailable[1] != snapshot.tof_unavailable[1];
            snapshot = next;
            have_snapshot = true;
        }
        if (have_snapshot) pegatron_monitor_tick(&snapshot, changed);
        const uint32_t now = millis();
        if (now - last_blink >= 500) {
            last_blink = now;
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state);
        }
        sleep_ms(2);
    }
}

#if USE_SORTER_ENGINE
static const char *fsmStateName(sorter_core::SystemState st) {
    switch (st) {
    case sorter_core::SystemState::Init:    return "INIT";
    case sorter_core::SystemState::Idle:    return "IDLE";
    case sorter_core::SystemState::Running: return "RUNNING";
    case sorter_core::SystemState::Manual:  return "MANUAL";
    case sorter_core::SystemState::Calib:   return "CALIB";
    case sorter_core::SystemState::Fault:   return "FAULT";
    case sorter_core::SystemState::Estop:   return "ESTOP";
    }
    return "?";
}

static void runCalibC1() {
    const bool was_running = (system_fsm.state() == sorter_core::SystemState::Running);
    if (was_running) {
        system_fsm.request_stop();
        sorting_enabled = false;
    }
    const uint32_t t_now = millis();
    for (Station &s : stations) {
        s.manual_test_active = false;
        stationRelease(s, t_now);
    }

    printf("[CLI] Dang thu 30 mau bang tai trong cho C1 (toi da 2.5s)...\n");
    constexpr std::size_t C1_TARGET = 30;
    double samples[2][C1_TARGET]{};
    std::size_t count[2]{};
    const uint32_t t_start = millis();

    while ((count[0] < C1_TARGET || count[1] < C1_TARGET) &&
           (millis() - t_start < 2500)) {
        for (int i = 0; i < 2; ++i) {
            if (stations[i].sensor_ok && count[i] < C1_TARGET && tofRead(stations[i]) &&
                stations[i].has_valid_target) {
                samples[i][count[i]++] = static_cast<double>(stations[i].distance_mm);
            }
        }
        sleep_ms(2);
    }

    for (int i = 0; i < 2; ++i) {
        Station &s = stations[i];
        if (count[i] == 0) {
            printf("[%s] CALIB C1 BO QUA: khong co du lieu ToF\n", s.name);
            continue;
        }
        const auto res = sorter_core::compute_c1_baseline(samples[i], count[i], 1.0);
        printf("[%s] CALIB C1 (%u mau): H0=%.1fmm, sigma0=%.2fmm -> %s\n",
               s.name, (unsigned)res.valid_count, res.h0_mm, res.sigma_mm,
               res.ok ? "DAT" : "NHIEU (>1mm)");
        s.detector_cfg.h0_mm = res.h0_mm;
        s.detector = sorter_core::ItemDetector(s.detector_cfg);
    }

    if (was_running) {
        system_fsm.request_start();
        sorting_enabled = (system_fsm.state() == sorter_core::SystemState::Running);
    }
}

static bool applyStationConfig(double l_min, double l_max, double m_min, double m_max,
                               uint32_t s1_on, uint32_t s1_td,
                               uint32_t s2_on, uint32_t s2_td, uint32_t min_off) {
    if (l_min < 0.0 || l_min >= l_max || l_max > 300.0 ||
        m_min < 0.0 || m_min >= m_max || m_max > 300.0 ||
        s1_on < 50 || s1_on > 3000 ||  // tram 1 la servo: s1_on = thoi gian giu day
        s2_on < 50 || s2_on > SOLENOID_MAX_ON_MS ||
        s1_td > 3000 || s2_td > 3000 ||
        min_off < 40 || min_off > 500) {
        return false;
    }
    const uint32_t on_vals[2] = { s1_on, s2_on };
    const uint32_t td_vals[2] = { s1_td, s2_td };
    for (int i = 0; i < 2; ++i) {
        Station &s = stations[i];
        s.on_time_ms = on_vals[i];
        if (s.servo) s.servo_gate.push_ms = on_vals[i];  // tram 1: s1_on = thoi gian giu servo o vi tri day
        s.travel_delay_ms = td_vals[i];
        s.geometry.fallback_delay_ms = td_vals[i];
        s.detector_cfg.use_ranges = true;
        s.detector_cfg.l_min_mm = l_min;
        s.detector_cfg.l_max_mm = l_max;
        s.detector_cfg.m_min_mm = m_min;
        s.detector_cfg.m_max_mm = m_max;
        s.detector.set_config(s.detector_cfg);
        s.actuator.set_config(sorter_core::SolenoidConfig{
            s.on_time_ms, SOLENOID_MAX_ON_MS, 60000, SOLENOID_RATED_DUTY, 0.80, min_off});
        if (i == 0) {
            s.min_mm = s.detector_cfg.h0_mm > l_max ? static_cast<uint16_t>(s.detector_cfg.h0_mm - l_max) : 0;
            s.max_mm = s.detector_cfg.h0_mm > l_min ? static_cast<uint16_t>(s.detector_cfg.h0_mm - l_min) : 0;
        } else {
            s.min_mm = s.detector_cfg.h0_mm > m_max ? static_cast<uint16_t>(s.detector_cfg.h0_mm - m_max) : 0;
            s.max_mm = s.detector_cfg.h0_mm > m_min ? static_cast<uint16_t>(s.detector_cfg.h0_mm - m_min) : 0;
        }
    }
    printf("[CFG] OK L=[%.1f,%.1f] M=[%.1f,%.1f] S1=(on=%lu,td=%lu) S2=(on=%lu,td=%lu) OFF=%lu\n",
           l_min, l_max, m_min, m_max,
           (unsigned long)s1_on, (unsigned long)s1_td,
           (unsigned long)s2_on, (unsigned long)s2_td, (unsigned long)min_off);
    return true;
}

static void handleSerialCommands() {
    static bool reading_cfg = false;
    static char cfg_buf[128]{};
    static size_t cfg_len = 0;

    if (reading_cfg) {
        for (;;) {
            const int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) return;
            if (ch == '\r' || ch == '\n') {
                cfg_buf[cfg_len] = '\0';
                double v1 = 0.0, v2 = 0.0, v3 = 0.0, v4 = 0.0;
                unsigned long a = 0, b = 0, c = 0, d = 0, e = 0;
                bool ok = false;
                if (sscanf(cfg_buf, "%lf %lf %lf %lf %lu %lu %lu %lu %lu",
                           &v1, &v2, &v3, &v4, &a, &b, &c, &d, &e) == 9) {
                    ok = applyStationConfig(v1, v2, v3, v4,
                                            (uint32_t)a, (uint32_t)b,
                                            (uint32_t)c, (uint32_t)d, (uint32_t)e);
                } else if (sscanf(cfg_buf, "%lf %lf %lu %lu %lu %lu %lu",
                                  &v1, &v2, &a, &b, &c, &d, &e) == 7 && v1 > 0.0 && v2 > v1) {
                    ok = applyStationConfig(v2, 200.0, v1, v2,
                                            (uint32_t)a, (uint32_t)b,
                                            (uint32_t)c, (uint32_t)d, (uint32_t)e);
                }
                if (!ok) printf("[CFG] ERR: %s\n", cfg_buf);
                reading_cfg = false;
                cfg_len = 0;
                return;
            }
            if (cfg_len + 1 < sizeof cfg_buf) {
                cfg_buf[cfg_len++] = static_cast<char>(ch);
            }
        }
    }

    const int ch = getchar_timeout_us(0);
    if (ch == PICO_ERROR_TIMEOUT || ch == '\r' || ch == '\n') return;

    const uint32_t now = millis();
    if (ch == 'k' || ch == 'K') {
        reading_cfg = true;
        cfg_len = 0;
        return;
    }
    if (ch == 'c' || ch == 'C') {
        runCalibC1();
    } else if (ch == 'p' || ch == 'P') {
        if (system_fsm.state() == sorter_core::SystemState::Running) {
            system_fsm.request_stop();
            sorting_enabled = false;
            for (Station &s : stations) {
                s.manual_test_active = false;
                stationRelease(s, now);
                s.detector.reset();
                s.state = ST_IDLE;
            }
            printf("[CLI] -> PAUSE (IDLE)\n");
        } else {
            if (system_fsm.state() == sorter_core::SystemState::Fault) {
                system_fsm.request_reset(false);
            }
            const auto r = system_fsm.request_start();
            sorting_enabled = (system_fsm.state() == sorter_core::SystemState::Running);
            printf("[CLI] -> START: %s (%s)\n", r.ok ? "RUNNING" : "TU CHOI", r.reason);
        }
    } else if (ch == '1' || ch == '2') {
        if (system_fsm.state() != sorter_core::SystemState::Idle) {
            printf("[CLI] Dang %s - nhan 'p' de tam dung truoc khi test Solenoid.\n",
                   fsmStateName(system_fsm.state()));
            return;
        }
        Station &s = stations[(ch == '1') ? 0 : 1];
        if (!s.sensor_ok) {
            printf("[%s] Bo qua test: TOF_UNAVAILABLE\n", s.name);
            return;
        }
        stationManualTest(s, now);
    } else if (ch == 's' || ch == 'S') {
        printf("[STATUS] FSM=%s | H0=(%.1f, %.1f) L=[%.1f,%.1f] M=[%.1f,%.1f] | S1=(on=%lu,td=%lu) S2=(on=%lu,td=%lu) | ",
               fsmStateName(system_fsm.state()),
               stations[0].detector_cfg.h0_mm, stations[1].detector_cfg.h0_mm,
               stations[0].detector_cfg.l_min_mm, stations[0].detector_cfg.l_max_mm,
               stations[0].detector_cfg.m_min_mm, stations[0].detector_cfg.m_max_mm,
               (unsigned long)stations[0].on_time_ms, (unsigned long)stations[0].travel_delay_ms,
               (unsigned long)stations[1].on_time_ms, (unsigned long)stations[1].travel_delay_ms);
        printf("T1(tot=%lu,push=%lu) T2(tot=%lu,push=%lu,miss=%lu)\n",
               (unsigned long)stations[0].total_count, (unsigned long)stations[0].push_count,
               (unsigned long)stations[1].total_count, (unsigned long)stations[1].push_count,
               (unsigned long)stations[1].missed_count);
        printf("[TOF1] raw=%u status=%u bad=%lu det_state=%u H0=%.1f missed=%lu\n",
               (unsigned)stations[0].raw_mm, (unsigned)stations[0].raw_status,
               (unsigned long)stations[0].bad_samples, (unsigned)stations[0].detector.state(),
               stations[0].detector_cfg.h0_mm, (unsigned long)stations[0].missed_count);
        printf("[SERVO1] LM393_DO=%u active=%u stage=%u pulse=%u us (HOME=%u PUSH=%u) push_ms=%lu miss=%lu\n",
               (unsigned)lm393Raw(), (unsigned)lm393Active(), (unsigned)stations[0].servo_gate.stage,
               (unsigned)stations[0].servo_us, (unsigned)SERVO_HOME_US, (unsigned)SERVO_PUSH_US,
               (unsigned long)stations[0].servo_gate.push_ms, (unsigned long)stations[0].missed_count);
    } else if (ch == '?' || ch == 'h' || ch == 'H') {
        printf("[CLI] c=Calib H0 | p=Pause/Run | 1/2=Test T1 servo/T2 solenoid | s=Status | k <l_min> <l_max> <m_min> <m_max> <s1_on> <s1_td> <s2_on> <s2_td> <off>\n");
    }
}
#endif

int main() {
    stdio_init_all();

    for (int i = 0; i < 30 && !stdio_usb_connected(); i++) sleep_ms(100);
    printf("\n=======================================================\n");
    printf("   BANG TAI PHAN LOAI TU DONG - Raspberry Pi Pico 2 W   \n");
    printf("=======================================================\n");
#if USE_SORTER_ENGINE
    printf("[CLI] c=Calib H0 | p=Pause/Run | 1/2=Test Solenoid | s=Status\n");
#endif

    for (int i = 0; i < 2; i++) {
        Station &s = stations[i];
        s.state = ST_IDLE;
        s.distance_mm = MAX_VALID_RANGE_MM;
        s.has_valid_target = false;
        s.total_count = 0;
        s.q_head = s.q_tail = s.q_count = 0;
#if USE_SORTER_ENGINE
        const int configured_ref = (i == 0) ? PEGATRON_REFERENCE_1_MM : PEGATRON_REFERENCE_2_MM;
        const double ref_mm = configured_ref > 0 ? static_cast<double>(configured_ref) : DEFAULT_H0_MM;
        sorter_core::StationConfig cfg{};
        cfg.station = static_cast<unsigned>(i + 1);
        cfg.h0_mm = ref_mm;
        cfg.detect_delta_mm = DETECT_DELTA_MM;
        cfg.clear_delta_mm = CLEAR_DELTA_MM;
        cfg.threshold_sm_mm = THRESHOLD_SM_MM;
        cfg.threshold_ml_mm = THRESHOLD_ML_MM;
        cfg.use_ranges = true;
        cfg.l_min_mm = L_HEIGHT_MIN_MM;
        cfg.l_max_mm = L_HEIGHT_MAX_MM;
        cfg.m_min_mm = M_HEIGHT_MIN_MM;
        cfg.m_max_mm = M_HEIGHT_MAX_MM;
        cfg.n_samples = MEDIAN_SAMPLES;
        cfg.clear_samples = CLEAR_COUNT;
        cfg.discard_first_edge_sample = true;
        cfg.use_peak = (i == 0);  // servo station waits on LM393 anyway, so decide on the item's peak height
        cfg.max_valid_distance_mm = static_cast<double>(MAX_VALID_RANGE_MM);
        s.detector_cfg = cfg;
        s.detector = sorter_core::ItemDetector(s.detector_cfg);
        s.actuator = sorter_core::SolenoidActuator(
            sorter_core::SolenoidConfig{s.on_time_ms, SOLENOID_MAX_ON_MS, 60000, SOLENOID_RATED_DUTY, 0.80, SOLENOID_MIN_OFF_MS});
        s.geometry = sorter_core::TriggerGeometry{0.0, 0.0, 0.0, 0.0, 0.0, s.travel_delay_ms};
        s.missed_count = 0;
        s.manual_test_active = false;
        s.servo = (i == 0);
#endif
        if (s.servo) servoInit(s); else solenoidInit(s.sol_pin);
        tofInit(s);
    }
#if USE_SORTER_ENGINE
    system_fsm.init_ok();
    system_fsm.request_start();
    sorting_enabled = (system_fsm.state() == sorter_core::SystemState::Running);
    // No fixed reference -> measure H0 on the EMPTY belt at boot (82 mm default is rarely right).
    if (PEGATRON_REFERENCE_1_MM <= 0 || PEGATRON_REFERENCE_2_MM <= 0) {
        printf("[BOOT] De BANG TAI TRONG: tu do H0...\n");
        runCalibC1();
    }
#endif

    const bool network_enabled = PEGATRON_WIFI_SSID[0] != '\0';
    if (network_enabled) {
        queue_init(&snapshot_queue, sizeof(pegatron_snapshot), 4);
        queue_init(&command_queue, sizeof(pegatron_command), 4);
        queue_init(&ack_queue, sizeof(pegatron_ack), 4);
        flash_safe_execute_core_init();  // Lets core 1 save Wi-Fi credentials to flash.
        multicore_launch_core1_with_stack(networkWorker, network_stack, sizeof network_stack);
    }
    uint32_t last_print = 0, last_snapshot = 0;

    while (true) {
#if USE_SORTER_ENGINE
        handleSerialCommands();
#endif
        stationUpdate(stations[0]);
        stationUpdate(stations[1]);

        uint32_t now = millis();
        if (now - last_print >= PRINT_PERIOD_MS) {
            last_print = now;
            printStatus();
        }
        if (network_enabled) runCommands();
        if (network_enabled && now - last_snapshot >= 100) {
            last_snapshot = now;
            const auto snapshot = makeSnapshot();
            queue_try_add(&snapshot_queue, &snapshot);
        }
    }
}
