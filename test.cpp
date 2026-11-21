#include <stdio.h>

#include "pico/stdlib.h"
#include "vl53l1x.h"

// ======================= CAU HINH CHAN =======================
#define TOF1_I2C        i2c0
#define TOF1_SDA_PIN    0
#define TOF1_SCL_PIN    1

#define TOF2_I2C        i2c1
#define TOF2_SDA_PIN    2
#define TOF2_SCL_PIN    3

static constexpr uint I2C_BAUDRATE = 100000;
static constexpr uint32_t REPORT_INTERVAL_MS = 250;
static constexpr uint32_t SAMPLE_TIMEOUT_MS = 800;
// GP10 va GP11 dieu khien solenoid tren bang tai. Giu LOW khi thu ToF.
static constexpr uint32_t SOLENOID_PINS = (1u << 10) | (1u << 11);

struct TofSensor {
    const char *name;
    i2c_inst_t *bus;
    uint sda_pin;
    uint scl_pin;
    VL53L1X driver;
    bool sda_idle = false;
    bool scl_idle = false;
    bool initialized = false;
    bool has_sample = false;
    uint16_t distance_mm = 0;
    uint32_t last_sample_ms = 0;

    TofSensor(const char *name, i2c_inst_t *bus, uint sda_pin, uint scl_pin)
        : name(name), bus(bus), sda_pin(sda_pin), scl_pin(scl_pin), driver(bus) {}
};

static void startSensor(TofSensor &sensor) {
    i2c_init(sensor.bus, I2C_BAUDRATE);
    gpio_set_function(sensor.sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(sensor.scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sensor.sda_pin);
    gpio_pull_up(sensor.scl_pin);
    sleep_ms(1);
    sensor.sda_idle = gpio_get(sensor.sda_pin);
    sensor.scl_idle = gpio_get(sensor.scl_pin);

    sensor.initialized = sensor.driver.init();
    if (sensor.initialized) {
        sensor.driver.startContinuous();
        sensor.last_sample_ms = to_ms_since_boot(get_absolute_time());
    }
}

static void pollSensor(TofSensor &sensor) {
    if (!sensor.initialized || !sensor.driver.dataReady()) return;

    sensor.distance_mm = sensor.driver.read(false);
    sensor.has_sample = true;
    sensor.last_sample_ms = to_ms_since_boot(get_absolute_time());
}

static void printReading(const TofSensor &sensor, uint32_t now_ms) {
    printf("%s=", sensor.name);
    if (!sensor.initialized) {
        const auto &diag = sensor.driver.initDiagnostic();
        printf("LOI_KHOI_TAO (stage=%s model=0x%04X boot=0x%02X SDA_IDLE=%u SCL_IDLE=%u",
               diag.stage, (unsigned)diag.model_id, (unsigned)diag.boot_status,
               (unsigned)sensor.sda_idle, (unsigned)sensor.scl_idle);
        if (diag.i2c_operation) {
            printf(" i2c=%s reg=0x%04X rc=%d", diag.i2c_operation, (unsigned)diag.i2c_register, diag.i2c_result);
        }
        printf(")");
    } else if (now_ms - sensor.last_sample_ms >= SAMPLE_TIMEOUT_MS) {
        printf("MAT_DU_LIEU");
    } else if (!sensor.has_sample) {
        printf("CHO_MAU");
    } else if (sensor.driver.ranging_data.range_status != 0 || sensor.distance_mm == 0) {
        printf("MAU_KHONG_HOP_LE (status=%u)", (unsigned)sensor.driver.ranging_data.range_status);
    } else {
        printf("%u mm", (unsigned)sensor.distance_mm);
    }
}

int main() {
    gpio_init_mask(SOLENOID_PINS);
    gpio_put_masked(SOLENOID_PINS, 0);
    gpio_set_dir_out_masked(SOLENOID_PINS);
    stdio_init_all();

    TofSensor tof1("TOF1", TOF1_I2C, TOF1_SDA_PIN, TOF1_SCL_PIN);
    TofSensor tof2("TOF2", TOF2_I2C, TOF2_SDA_PIN, TOF2_SCL_PIN);
    startSensor(tof1);
    startSensor(tof2);

    uint32_t last_report_ms = to_ms_since_boot(get_absolute_time()) - REPORT_INTERVAL_MS;
    while (true) {
        pollSensor(tof1);
        pollSensor(tof2);

        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_report_ms >= REPORT_INTERVAL_MS) {
            printReading(tof1, now_ms);
            printf(" | ");
            printReading(tof2, now_ms);
            printf("\n");
            last_report_ms = now_ms;
        }

        sleep_ms(5);
    }
}
