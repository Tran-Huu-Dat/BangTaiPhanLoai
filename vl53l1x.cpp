/**
 * Driver cho cam bien ToF VL53L4CD / VL53L1X (Model ID 0xEBAA) tren Pico SDK
 */
#include "vl53l1x.h"
#include "pico/stdlib.h"

static const uint32_t I2C_TIMEOUT_US = 15000;

// Bang khoi tao 91 byte chuan ST cho VL53L4CD (bat dau tu thanh ghi 0x002D)
static const uint8_t VL53L4CD_INIT_SEQ[91] = {
    0x12, // 0x2d
    0x00, // 0x2e
    0x00, // 0x2f
    0x11, // 0x30 : active low interrupt
    0x02, // 0x31
    0x00, // 0x32
    0x02, // 0x33
    0x08, // 0x34
    0x00, // 0x35
    0x08, // 0x36
    0x10, // 0x37
    0x01, // 0x38
    0x01, // 0x39
    0x00, // 0x3a
    0x00, // 0x3b
    0x00, // 0x3c
    0x00, // 0x3d
    0xff, // 0x3e
    0x00, // 0x3f
    0x0f, // 0x40
    0x00, // 0x41
    0x00, // 0x42
    0x00, // 0x43
    0x00, // 0x44
    0x00, // 0x45
    0x20, // 0x46 : new sample ready
    0x0b, // 0x47
    0x00, // 0x48
    0x00, // 0x49
    0x02, // 0x4a
    0x14, // 0x4b
    0x21, // 0x4c
    0x00, // 0x4d
    0x00, // 0x4e
    0x05, // 0x4f
    0x00, // 0x50
    0x00, // 0x51
    0x00, // 0x52
    0x00, // 0x53
    0xc8, // 0x54
    0x00, // 0x55
    0x00, // 0x56
    0x38, // 0x57
    0xff, // 0x58
    0x01, // 0x59
    0x00, // 0x5a
    0x08, // 0x5b
    0x00, // 0x5c
    0x00, // 0x5d
    0x01, // 0x5e
    0xcc, // 0x5f
    0x07, // 0x60
    0x01, // 0x61
    0xf1, // 0x62
    0x05, // 0x63
    0x00, // 0x64
    0xa0, // 0x65
    0x00, // 0x66
    0x80, // 0x67
    0x08, // 0x68
    0x38, // 0x69
    0x00, // 0x6a
    0x00, // 0x6b
    0x00, // 0x6c
    0x00, // 0x6d
    0x0f, // 0x6e
    0x89, // 0x6f
    0x00, // 0x70
    0x00, // 0x71
    0x00, // 0x72
    0x00, // 0x73
    0x00, // 0x74
    0x00, // 0x75
    0x00, // 0x76
    0x01, // 0x77
    0x07, // 0x78
    0x05, // 0x79
    0x06, // 0x7a
    0x06, // 0x7b
    0x00, // 0x7c
    0x00, // 0x7d
    0x02, // 0x7e
    0xc7, // 0x7f
    0xff, // 0x80
    0x9b, // 0x81
    0x00, // 0x82
    0x00, // 0x83
    0x00, // 0x84
    0x01, // 0x85
    0x00, // 0x86
    0x00  // 0x87
};

// Bang anh xa ma trang thai cua VL53L4CD (0 = RANGE_VALID)
static const uint8_t STATUS_RTN[24] = {
    255, 255, 255, 5, 2, 4, 1, 7, 3,
    0, // index 9 -> RANGE_VALID (0)
    255, 255, 9, 255, 255, 255, 255, 255, 10, 6, 255, 255, 11, 12
};

VL53L1X::VL53L1X(i2c_inst_t *i2c_inst, uint8_t addr)
    : i2c(i2c_inst), address(addr), i2c_error(false)
{
    ranging_data.range_mm = 0;
    ranging_data.range_status = 255;
}

bool VL53L1X::checkTransfer(int result, int expected, uint16_t reg, const char *operation) {
    if (result == expected) return true;
    if (!i2c_error) {
        init_diagnostic.i2c_operation = operation;
        init_diagnostic.i2c_register = reg;
        init_diagnostic.i2c_result = result;
    }
    i2c_error = true;
    return false;
}

void VL53L1X::writeReg(uint16_t reg, uint8_t val) {
    uint8_t buf[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
    checkTransfer(i2c_write_timeout_us(i2c, address, buf, 3, false, I2C_TIMEOUT_US), 3, reg, "WRITE");
}

void VL53L1X::writeReg16Bit(uint16_t reg, uint16_t val) {
    uint8_t buf[4] = {
        (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
        (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)
    };
    checkTransfer(i2c_write_timeout_us(i2c, address, buf, 4, false, I2C_TIMEOUT_US), 4, reg, "WRITE");
}

void VL53L1X::writeReg32Bit(uint16_t reg, uint32_t val) {
    uint8_t buf[6] = {
        (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF),
        (uint8_t)(val >> 24), (uint8_t)(val >> 16),
        (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)
    };
    checkTransfer(i2c_write_timeout_us(i2c, address, buf, 6, false, I2C_TIMEOUT_US), 6, reg, "WRITE");
}

void VL53L1X::writeMulti(uint16_t reg, const uint8_t *src, uint16_t count) {
    uint8_t buf[96];
    if (count > 92) return;
    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    for (uint16_t i = 0; i < count; i++) buf[2 + i] = src[i];
    int result = i2c_write_timeout_us(i2c, address, buf, count + 2, false, I2C_TIMEOUT_US * 2);
    checkTransfer(result, count + 2, reg, "WRITE");
}

bool VL53L1X::readRegister(uint16_t reg, uint8_t *data, uint8_t count) {
    uint8_t reg_buf[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    int result = i2c_write_timeout_us(i2c, address, reg_buf, 2, true, I2C_TIMEOUT_US);
    if (!checkTransfer(result, 2, reg, "SELECT_REG")) return false;
    result = i2c_read_timeout_us(i2c, address, data, count, false, I2C_TIMEOUT_US);
    return checkTransfer(result, count, reg, "READ");
}

uint8_t VL53L1X::readReg(uint16_t reg) {
    uint8_t val;
    if (!readRegister(reg, &val, 1)) return 0xFF;
    return val;
}

uint16_t VL53L1X::readReg16Bit(uint16_t reg) {
    uint8_t val[2];
    if (!readRegister(reg, val, 2)) return 0;
    return (uint16_t)((val[0] << 8) | val[1]);
}

uint32_t VL53L1X::readReg32Bit(uint16_t reg) {
    uint8_t val[4];
    if (!readRegister(reg, val, 4)) return 0;
    return ((uint32_t)val[0] << 24) | ((uint32_t)val[1] << 16) | ((uint32_t)val[2] << 8) | val[3];
}

void VL53L1X::clearInterrupt() {
    writeReg(0x0086, 0x01);
}

uint8_t VL53L1X::interruptPolarity() {
    uint8_t int_pol = (readReg(0x0030) & 0x10) >> 4;
    return int_pol ? 0 : 1;
}

bool VL53L1X::dataReady() {
    return (readReg(0x0031) & 0x01) == interruptPolarity();
}

bool VL53L1X::init(bool io_2v8) {
    (void)io_2v8;
    i2c_error = false;
    init_diagnostic = {};

    // 1. Kiem tra Model ID (0x010F -> 0xEBAA hoac 0xEACC)
    init_diagnostic.model_id = readReg16Bit(0x010F);
    uint8_t model_id = init_diagnostic.model_id >> 8;
    if (model_id != 0xEB && model_id != 0xEA) {
        return false;
    }

    // 2. Soft reset de dam bao trang thai sach
    init_diagnostic.stage = "BOOT";
    writeReg(0x0000, 0x00);
    sleep_ms(2);
    writeReg(0x0000, 0x01);
    sleep_ms(5);

    // 3. Cho chip boot xong (FIRMWARE_SYSTEM_STATUS 0x00E5 == 0x03)
    bool booted = false;
    for (int i = 0; i < 500; i++) {
        init_diagnostic.boot_status = readReg(0x00E5);
        if (init_diagnostic.boot_status == 0x03) {
            booted = true;
            break;
        }
        sleep_ms(1);
    }
    if (!booted) return false;

    // 4. Nap bang 91 byte cau hinh vao 0x002D
    if (!i2c_error) init_diagnostic.stage = "FIRST_MEASUREMENT";
    writeMulti(0x002D, VL53L4CD_INIT_SEQ, 91);

    // 5. Chay VHV calibration (start_vhv)
    writeReg(0x0087, 0x40); // Start ranging autonomous
    bool ready = false;
    for (int i = 0; i < 1000; i++) {
        if (dataReady()) {
            ready = true;
            break;
        }
        sleep_ms(1);
    }
    if (!ready) return false;

    if (!i2c_error) init_diagnostic.stage = "CONFIG";
    clearInterrupt();
    stopContinuous();

    writeReg(0x0008, 0x09);     // VHV_CONFIG_TIMEOUT_MACROP_LOOP_BOUND
    writeReg(0x000B, 0x00);
    writeReg16Bit(0x0024, 0x0500);

    // 6. Dat che do continuous (inter_measurement = 0) va timing budget = 40ms
    writeReg32Bit(0x006C, 0);
    setMeasurementTimingBudget(40000);

    return !i2c_error;
}

bool VL53L1X::setMeasurementTimingBudget(uint32_t budget_us) {
    uint16_t osc_freq = readReg16Bit(0x0006);
    if (osc_freq == 0) return false;

    uint32_t macro_period_us = (uint32_t)(2304.0f * (1073741824.0f / (float)osc_freq)) >> 6;
    uint32_t inter_meas = readReg32Bit(0x006C);

    if (inter_meas == 0) {
        budget_us -= 2500;
    } else {
        budget_us -= 4300;
        budget_us /= 2;
    }

    // RANGE_CONFIG_A (0x005E)
    uint32_t tb_shifted = budget_us << 12;
    uint32_t tmp = macro_period_us * 16;
    uint32_t ls_byte = ((tb_shifted + ((tmp >> 6) >> 1)) / (tmp >> 6)) - 1;
    uint16_t ms_byte = 0;
    while ((ls_byte >> 8) & 0xFFFFFF) {
        ls_byte >>= 1;
        ms_byte++;
    }
    writeReg16Bit(0x005E, (uint16_t)((ms_byte << 8) | (ls_byte & 0xFF)));

    // RANGE_CONFIG_B (0x0061)
    tmp = macro_period_us * 12;
    ls_byte = ((tb_shifted + ((tmp >> 6) >> 1)) / (tmp >> 6)) - 1;
    ms_byte = 0;
    while ((ls_byte >> 8) & 0xFFFFFF) {
        ls_byte >>= 1;
        ms_byte++;
    }
    writeReg16Bit(0x0061, (uint16_t)((ms_byte << 8) | (ls_byte & 0xFF)));

    return true;
}

void VL53L1X::startContinuous(uint32_t period_ms) {
    (void)period_ms;
    // Continuous mode: inter_measurement = 0 -> write 0x21 to SYSTEM_START (0x0087)
    writeReg32Bit(0x006C, 0);
    writeReg(0x0087, 0x21);

    // Cho mau dau tien san sang va xoa ngat
    for (int i = 0; i < 500; i++) {
        if (dataReady()) {
            clearInterrupt();
            break;
        }
        sleep_ms(1);
    }
}

void VL53L1X::stopContinuous() {
    writeReg(0x0087, 0x00);
}

uint16_t VL53L1X::read(bool blocking) {
    if (blocking) {
        for (int i = 0; i < 500; i++) {
            if (dataReady()) break;
            sleep_ms(1);
        }
    }

    uint8_t raw_status = readReg(0x0089) & 0x1F;
    uint8_t status = (raw_status < 24) ? STATUS_RTN[raw_status] : 255;

    uint16_t dist_mm = readReg16Bit(0x0096);
    clearInterrupt();

    ranging_data.range_status = status;
    ranging_data.range_mm = dist_mm;
    return dist_mm;
}
