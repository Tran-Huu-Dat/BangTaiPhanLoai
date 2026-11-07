/**
 * vl53l0x.h - Driver toi gian cho cam bien ToF VL53L0X tren Pico SDK
 * (Port tu thu vien Pololu VL53L0X-arduino, ho tro nhieu bus I2C doc lap)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "hardware/i2c.h"

class VL53L0X {
public:
    static const uint8_t DEFAULT_ADDR = 0x29;   // Dia chi I2C mac dinh
    static const uint16_t OUT_OF_RANGE = 8190;  // Gia tri >= nay = khong co vat

    explicit VL53L0X(i2c_inst_t *i2c, uint8_t addr = DEFAULT_ADDR)
        : i2c_(i2c), addr_(addr) {}

    // Khoi tao cam bien. io_2v8 = true neu module chay I/O 2.8V-3.3V (da so module ban san)
    bool init(bool io_2v8 = true);

    // Bat che do do lien tuc (back-to-back)
    void startContinuous();

    // Kiem tra da co ket qua do moi chua (khong chan CPU)
    bool dataReady();

    // Doc ket qua (mm). Chi goi khi dataReady() == true
    uint16_t readRangeMm();

    // Dat thoi gian do (us). Lon hon => chinh xac hon nhung cham hon. Mac dinh ~33000us
    bool setMeasurementTimingBudget(uint32_t budget_us);

    bool lastI2cError() const { return i2c_error_; }

private:
    struct SequenceStepEnables { bool tcc, msrc, dss, pre_range, final_range; };
    struct SequenceStepTimeouts {
        uint16_t pre_range_vcsel_period_pclks, final_range_vcsel_period_pclks;
        uint16_t msrc_dss_tcc_mclks, pre_range_mclks, final_range_mclks;
        uint32_t msrc_dss_tcc_us, pre_range_us, final_range_us;
    };

    i2c_inst_t *i2c_;
    uint8_t addr_;
    uint8_t stop_variable_ = 0;
    uint32_t timing_budget_us_ = 0;
    bool i2c_error_ = false;

    // Ham I2C muc thap
    void writeReg(uint8_t reg, uint8_t val);
    void writeReg16(uint8_t reg, uint16_t val);
    void writeMulti(uint8_t reg, const uint8_t *src, uint8_t count);
    uint8_t readReg(uint8_t reg);
    uint16_t readReg16(uint8_t reg);
    void readMulti(uint8_t reg, uint8_t *dst, uint8_t count);

    // Ham noi bo
    bool setSignalRateLimit(float limit_mcps);
    bool getSpadInfo(uint8_t *count, bool *type_is_aperture);
    uint32_t getMeasurementTimingBudget();
    void getSequenceStepEnables(SequenceStepEnables *en);
    void getSequenceStepTimeouts(const SequenceStepEnables *en, SequenceStepTimeouts *to);
    uint8_t getVcselPulsePeriod(bool final_range);
    bool performSingleRefCalibration(uint8_t vhv_init_byte);

    static uint16_t decodeTimeout(uint16_t reg_val);
    static uint16_t encodeTimeout(uint32_t timeout_mclks);
    static uint32_t timeoutMclksToMicroseconds(uint16_t mclks, uint8_t vcsel_pclks);
    static uint32_t timeoutMicrosecondsToMclks(uint32_t us, uint8_t vcsel_pclks);
};

