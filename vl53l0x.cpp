/**
 * vl53l0x.cpp - Driver toi gian cho cam bien ToF VL53L0X tren Pico SDK
 * (Port tu thu vien Pololu VL53L0X-arduino)
 */
#include "vl53l0x.h"
#include "pico/stdlib.h"

// ---- Dia chi thanh ghi ----
enum {
    SYSRANGE_START                         = 0x00,
    SYSTEM_SEQUENCE_CONFIG                 = 0x01,
    SYSTEM_INTERRUPT_CONFIG_GPIO           = 0x0A,
    SYSTEM_INTERRUPT_CLEAR                 = 0x0B,
    RESULT_INTERRUPT_STATUS                = 0x13,
    RESULT_RANGE_STATUS                    = 0x14,
    FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT = 0x44,
    MSRC_CONFIG_TIMEOUT_MACROP             = 0x46,
    PRE_RANGE_CONFIG_VCSEL_PERIOD          = 0x50,
    PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI     = 0x51,
    MSRC_CONFIG_CONTROL                    = 0x60,
    FINAL_RANGE_CONFIG_VCSEL_PERIOD        = 0x70,
    FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI   = 0x71,
    GPIO_HV_MUX_ACTIVE_HIGH                = 0x84,
    VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV      = 0x89,
    GLOBAL_CONFIG_SPAD_ENABLES_REF_0       = 0xB0,
    GLOBAL_CONFIG_REF_EN_START_SELECT      = 0xB6,
    DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD    = 0x4E,
    DYNAMIC_SPAD_REF_EN_START_OFFSET       = 0x4F,
    IDENTIFICATION_MODEL_ID                = 0xC0,
};

static const uint32_t I2C_TIMEOUT_US = 5000;   // Timeout cho moi giao dich I2C
static const uint32_t IO_TIMEOUT_MS  = 500;    // Timeout cho vong cho cam bien

#define decodeVcselPeriod(reg_val)  (((reg_val) + 1) << 1)
#define calcMacroPeriod(vcsel)      ((((uint32_t)2304 * (vcsel) * 1655) + 500) / 1000)

// =====================================================================
//                       HAM I2C MUC THAP
// =====================================================================
void VL53L0X::writeReg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    if (i2c_write_timeout_us(i2c_, addr_, buf, 2, false, I2C_TIMEOUT_US) != 2) i2c_error_ = true;
}

void VL53L0X::writeReg16(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    if (i2c_write_timeout_us(i2c_, addr_, buf, 3, false, I2C_TIMEOUT_US) != 3) i2c_error_ = true;
}

void VL53L0X::writeMulti(uint8_t reg, const uint8_t *src, uint8_t count) {
    uint8_t buf[16];
    buf[0] = reg;
    for (uint8_t i = 0; i < count && i < 15; i++) buf[i + 1] = src[i];
    if (i2c_write_timeout_us(i2c_, addr_, buf, count + 1, false, I2C_TIMEOUT_US) != count + 1) i2c_error_ = true;
}

uint8_t VL53L0X::readReg(uint8_t reg) {
    uint8_t val = 0;
    readMulti(reg, &val, 1);
    return val;
}

uint16_t VL53L0X::readReg16(uint8_t reg) {
    uint8_t buf[2] = {0, 0};
    readMulti(reg, buf, 2);
    return (uint16_t)((buf[0] << 8) | buf[1]);
}

void VL53L0X::readMulti(uint8_t reg, uint8_t *dst, uint8_t count) {
    if (i2c_write_timeout_us(i2c_, addr_, &reg, 1, true, I2C_TIMEOUT_US) != 1) { i2c_error_ = true; return; }
    if (i2c_read_timeout_us(i2c_, addr_, dst, count, false, I2C_TIMEOUT_US) != count) i2c_error_ = true;
}

// =====================================================================
//                       KHOI TAO CAM BIEN
// =====================================================================
bool VL53L0X::init(bool io_2v8) {
    i2c_error_ = false;

    // Kiem tra Model ID (VL53L0X luon tra ve 0xEE)
    if (readReg(IDENTIFICATION_MODEL_ID) != 0xEE) return false;

    if (io_2v8) {
        writeReg(VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV, readReg(VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV) | 0x01);
    }

    // "Set I2C standard mode"
    writeReg(0x88, 0x00);

    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    stop_variable_ = readReg(0x91);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);

    // Tat kiem tra SIGNAL_RATE_MSRC va SIGNAL_RATE_PRE_RANGE
    writeReg(MSRC_CONFIG_CONTROL, readReg(MSRC_CONFIG_CONTROL) | 0x12);
    setSignalRateLimit(0.25f);
    writeReg(SYSTEM_SEQUENCE_CONFIG, 0xFF);

    // ---- Cau hinh SPAD tham chieu ----
    uint8_t spad_count;
    bool spad_type_is_aperture;
    if (!getSpadInfo(&spad_count, &spad_type_is_aperture)) return false;

    uint8_t ref_spad_map[6];
    readMulti(GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6);

    writeReg(0xFF, 0x01);
    writeReg(DYNAMIC_SPAD_REF_EN_START_OFFSET, 0x00);
    writeReg(DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD, 0x2C);
    writeReg(0xFF, 0x00);
    writeReg(GLOBAL_CONFIG_REF_EN_START_SELECT, 0xB4);

    uint8_t first_spad_to_enable = spad_type_is_aperture ? 12 : 0;
    uint8_t spads_enabled = 0;
    for (uint8_t i = 0; i < 48; i++) {
        if (i < first_spad_to_enable || spads_enabled == spad_count) {
            ref_spad_map[i / 8] &= ~(1 << (i % 8));
        } else if ((ref_spad_map[i / 8] >> (i % 8)) & 0x1) {
            spads_enabled++;
        }
    }
    writeMulti(GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6);

    // ---- Tuning settings mac dinh cua ST ----
    static const uint8_t tuning[][2] = {
        {0xFF,0x01},{0x00,0x00},{0xFF,0x00},{0x09,0x00},{0x10,0x00},{0x11,0x00},
        {0x24,0x01},{0x25,0xFF},{0x75,0x00},{0xFF,0x01},{0x4E,0x2C},{0x48,0x00},
        {0x30,0x20},{0xFF,0x00},{0x30,0x09},{0x54,0x00},{0x31,0x04},{0x32,0x03},
        {0x40,0x83},{0x46,0x25},{0x60,0x00},{0x27,0x00},{0x50,0x06},{0x51,0x00},
        {0x52,0x96},{0x56,0x08},{0x57,0x30},{0x61,0x00},{0x62,0x00},{0x64,0x00},
        {0x65,0x00},{0x66,0xA0},{0xFF,0x01},{0x22,0x32},{0x47,0x14},{0x49,0xFF},
        {0x4A,0x00},{0xFF,0x00},{0x7A,0x0A},{0x7B,0x00},{0x78,0x21},{0xFF,0x01},
        {0x23,0x34},{0x42,0x00},{0x44,0xFF},{0x45,0x26},{0x46,0x05},{0x40,0x40},
        {0x0E,0x06},{0x20,0x1A},{0x43,0x40},{0xFF,0x00},{0x34,0x03},{0x35,0x44},
        {0xFF,0x01},{0x31,0x04},{0x4B,0x09},{0x4C,0x05},{0x4D,0x04},{0xFF,0x00},
        {0x44,0x00},{0x45,0x20},{0x47,0x08},{0x48,0x28},{0x67,0x00},{0x70,0x04},
        {0x71,0x01},{0x72,0xFE},{0x76,0x00},{0x77,0x00},{0xFF,0x01},{0x0D,0x01},
        {0xFF,0x00},{0x80,0x01},{0x01,0xF8},{0xFF,0x01},{0x8E,0x01},{0x00,0x01},
        {0xFF,0x00},{0x80,0x00},
    };
    for (unsigned i = 0; i < sizeof(tuning) / sizeof(tuning[0]); i++) {
        writeReg(tuning[i][0], tuning[i][1]);
    }

    // ---- Cau hinh ngat: bao khi co mau moi ----
    writeReg(SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04);
    writeReg(GPIO_HV_MUX_ACTIVE_HIGH, readReg(GPIO_HV_MUX_ACTIVE_HIGH) & ~0x10);
    writeReg(SYSTEM_INTERRUPT_CLEAR, 0x01);

    timing_budget_us_ = getMeasurementTimingBudget();
    writeReg(SYSTEM_SEQUENCE_CONFIG, 0xE8);   // Tat MSRC va TCC
    setMeasurementTimingBudget(timing_budget_us_);

    // ---- Hieu chuan tham chieu (VHV + Phase) ----
    writeReg(SYSTEM_SEQUENCE_CONFIG, 0x01);
    if (!performSingleRefCalibration(0x40)) return false;
    writeReg(SYSTEM_SEQUENCE_CONFIG, 0x02);
    if (!performSingleRefCalibration(0x00)) return false;
    writeReg(SYSTEM_SEQUENCE_CONFIG, 0xE8);

    return !i2c_error_;
}

// =====================================================================
//                       DO KHOANG CACH
// =====================================================================
void VL53L0X::startContinuous() {
    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    writeReg(0x91, stop_variable_);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);
    writeReg(SYSRANGE_START, 0x02);   // Che do back-to-back
}

bool VL53L0X::dataReady() {
    return (readReg(RESULT_INTERRUPT_STATUS) & 0x07) != 0;
}

uint16_t VL53L0X::readRangeMm() {
    uint16_t range = readReg16(RESULT_RANGE_STATUS + 10);
    writeReg(SYSTEM_INTERRUPT_CLEAR, 0x01);
    return range;
}

// =====================================================================
//                       HAM NOI BO
// =====================================================================
bool VL53L0X::setSignalRateLimit(float limit_mcps) {
    if (limit_mcps < 0 || limit_mcps > 511.99f) return false;
    writeReg16(FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT, (uint16_t)(limit_mcps * (1 << 7)));
    return true;
}

bool VL53L0X::getSpadInfo(uint8_t *count, bool *type_is_aperture) {
    writeReg(0x80, 0x01);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x00);
    writeReg(0xFF, 0x06);
    writeReg(0x83, readReg(0x83) | 0x04);
    writeReg(0xFF, 0x07);
    writeReg(0x81, 0x01);
    writeReg(0x80, 0x01);
    writeReg(0x94, 0x6b);
    writeReg(0x83, 0x00);

    absolute_time_t deadline = make_timeout_time_ms(IO_TIMEOUT_MS);
    while (readReg(0x83) == 0x00) {
        if (time_reached(deadline) || i2c_error_) return false;
    }
    writeReg(0x83, 0x01);
    uint8_t tmp = readReg(0x92);
    *count = tmp & 0x7F;
    *type_is_aperture = (tmp >> 7) & 0x01;

    writeReg(0x81, 0x00);
    writeReg(0xFF, 0x06);
    writeReg(0x83, readReg(0x83) & ~0x04);
    writeReg(0xFF, 0x01);
    writeReg(0x00, 0x01);
    writeReg(0xFF, 0x00);
    writeReg(0x80, 0x00);
    return true;
}

bool VL53L0X::performSingleRefCalibration(uint8_t vhv_init_byte) {
    writeReg(SYSRANGE_START, 0x01 | vhv_init_byte);
    absolute_time_t deadline = make_timeout_time_ms(IO_TIMEOUT_MS);
    while ((readReg(RESULT_INTERRUPT_STATUS) & 0x07) == 0) {
        if (time_reached(deadline) || i2c_error_) return false;
    }
    writeReg(SYSTEM_INTERRUPT_CLEAR, 0x01);
    writeReg(SYSRANGE_START, 0x00);
    return true;
}

uint8_t VL53L0X::getVcselPulsePeriod(bool final_range) {
    uint8_t reg = final_range ? FINAL_RANGE_CONFIG_VCSEL_PERIOD : PRE_RANGE_CONFIG_VCSEL_PERIOD;
    return decodeVcselPeriod(readReg(reg));
}

void VL53L0X::getSequenceStepEnables(SequenceStepEnables *en) {
    uint8_t cfg = readReg(SYSTEM_SEQUENCE_CONFIG);
    en->tcc         = (cfg >> 4) & 0x1;
    en->dss         = (cfg >> 3) & 0x1;
    en->msrc        = (cfg >> 2) & 0x1;
    en->pre_range   = (cfg >> 6) & 0x1;
    en->final_range = (cfg >> 7) & 0x1;
}

void VL53L0X::getSequenceStepTimeouts(const SequenceStepEnables *en, SequenceStepTimeouts *to) {
    to->pre_range_vcsel_period_pclks = getVcselPulsePeriod(false);
    to->msrc_dss_tcc_mclks = readReg(MSRC_CONFIG_TIMEOUT_MACROP) + 1;
    to->msrc_dss_tcc_us = timeoutMclksToMicroseconds(to->msrc_dss_tcc_mclks, to->pre_range_vcsel_period_pclks);
    to->pre_range_mclks = decodeTimeout(readReg16(PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI));
    to->pre_range_us = timeoutMclksToMicroseconds(to->pre_range_mclks, to->pre_range_vcsel_period_pclks);

    to->final_range_vcsel_period_pclks = getVcselPulsePeriod(true);
    to->final_range_mclks = decodeTimeout(readReg16(FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI));
    if (en->pre_range) to->final_range_mclks -= to->pre_range_mclks;
    to->final_range_us = timeoutMclksToMicroseconds(to->final_range_mclks, to->final_range_vcsel_period_pclks);
}

uint32_t VL53L0X::getMeasurementTimingBudget() {
    const uint16_t StartOverhead = 1910, EndOverhead = 960, MsrcOverhead = 660,
                   TccOverhead = 590, DssOverhead = 690, PreRangeOverhead = 660,
                   FinalRangeOverhead = 550;
    SequenceStepEnables en;
    SequenceStepTimeouts to;
    uint32_t budget_us = StartOverhead + EndOverhead;

    getSequenceStepEnables(&en);
    getSequenceStepTimeouts(&en, &to);

    if (en.tcc) budget_us += to.msrc_dss_tcc_us + TccOverhead;
    if (en.dss) budget_us += 2 * (to.msrc_dss_tcc_us + DssOverhead);
    else if (en.msrc) budget_us += to.msrc_dss_tcc_us + MsrcOverhead;
    if (en.pre_range) budget_us += to.pre_range_us + PreRangeOverhead;
    if (en.final_range) budget_us += to.final_range_us + FinalRangeOverhead;

    timing_budget_us_ = budget_us;
    return budget_us;
}

bool VL53L0X::setMeasurementTimingBudget(uint32_t budget_us) {
    const uint16_t StartOverhead = 1910, EndOverhead = 960, MsrcOverhead = 660,
                   TccOverhead = 590, DssOverhead = 690, PreRangeOverhead = 660,
                   FinalRangeOverhead = 550;
    SequenceStepEnables en;
    SequenceStepTimeouts to;
    uint32_t used_us = StartOverhead + EndOverhead;

    getSequenceStepEnables(&en);
    getSequenceStepTimeouts(&en, &to);

    if (en.tcc) used_us += to.msrc_dss_tcc_us + TccOverhead;
    if (en.dss) used_us += 2 * (to.msrc_dss_tcc_us + DssOverhead);
    else if (en.msrc) used_us += to.msrc_dss_tcc_us + MsrcOverhead;
    if (en.pre_range) used_us += to.pre_range_us + PreRangeOverhead;

    if (en.final_range) {
        used_us += FinalRangeOverhead;
        if (used_us > budget_us) return false;   // Budget qua nho

        uint32_t final_us = budget_us - used_us;
        uint32_t final_mclks = timeoutMicrosecondsToMclks(final_us, to.final_range_vcsel_period_pclks);
        if (en.pre_range) final_mclks += to.pre_range_mclks;

        writeReg16(FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI, encodeTimeout(final_mclks));
        timing_budget_us_ = budget_us;
    }
    return true;
}

uint16_t VL53L0X::decodeTimeout(uint16_t reg_val) {
    return (uint16_t)((reg_val & 0x00FF) << (uint16_t)((reg_val & 0xFF00) >> 8)) + 1;
}

uint16_t VL53L0X::encodeTimeout(uint32_t timeout_mclks) {
    if (timeout_mclks == 0) return 0;
    uint32_t ls_byte = timeout_mclks - 1;
    uint16_t ms_byte = 0;
    while ((ls_byte & 0xFFFFFF00) > 0) { ls_byte >>= 1; ms_byte++; }
    return (uint16_t)((ms_byte << 8) | (ls_byte & 0xFF));
}

uint32_t VL53L0X::timeoutMclksToMicroseconds(uint16_t mclks, uint8_t vcsel_pclks) {
    uint32_t macro_period_ns = calcMacroPeriod(vcsel_pclks);
    return ((mclks * macro_period_ns) + 500) / 1000;
}

uint32_t VL53L0X::timeoutMicrosecondsToMclks(uint32_t us, uint8_t vcsel_pclks) {
    uint32_t macro_period_ns = calcMacroPeriod(vcsel_pclks);
    return ((us * 1000) + (macro_period_ns / 2)) / macro_period_ns;
}

