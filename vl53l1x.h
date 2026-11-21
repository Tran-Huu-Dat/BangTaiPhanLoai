#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

class VL53L1X {
public:
    static const uint8_t DEFAULT_ADDR = 0x29;

    enum DistanceMode { Short, Medium, Long, Unknown };

    struct RangingData {
        uint16_t range_mm;
        uint8_t  range_status; // 0 = Valid
    };

    struct InitDiagnostic {
        const char *stage = "MODEL_ID";
        uint16_t model_id = 0;
        uint8_t boot_status = 0;
        const char *i2c_operation = nullptr;
        uint16_t i2c_register = 0;
        int i2c_result = 0;
    };

    RangingData ranging_data;

    explicit VL53L1X(i2c_inst_t *i2c, uint8_t addr = DEFAULT_ADDR);

    bool init(bool io_2v8 = true);
    const InitDiagnostic &initDiagnostic() const { return init_diagnostic; }
    bool setDistanceMode(DistanceMode mode) { (void)mode; return true; }
    bool setMeasurementTimingBudget(uint32_t budget_us);
    void startContinuous(uint32_t period_ms = 0);
    void stopContinuous();

    bool dataReady();
    uint16_t read(bool blocking = false);

private:
    i2c_inst_t *i2c;
    uint8_t     address;
    bool        i2c_error;
    InitDiagnostic init_diagnostic;

    bool checkTransfer(int result, int expected, uint16_t reg, const char *operation);
    bool readRegister(uint16_t reg, uint8_t *data, uint8_t count);
    void writeReg(uint16_t reg, uint8_t val);
    void writeReg16Bit(uint16_t reg, uint16_t val);
    void writeReg32Bit(uint16_t reg, uint32_t val);
    void writeMulti(uint16_t reg, const uint8_t *src, uint16_t count);

    uint8_t  readReg(uint16_t reg);
    uint16_t readReg16Bit(uint16_t reg);
    uint32_t readReg32Bit(uint16_t reg);

    void clearInterrupt();
    uint8_t interruptPolarity();
};
