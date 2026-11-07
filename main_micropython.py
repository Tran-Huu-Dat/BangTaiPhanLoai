"""
BANG TAI PHAN LOAI - Raspberry Pi Pico 2 W (MicroPython)
2 tram chay doc lap va dong thoi (Non-blocking bang time.ticks_ms)

Ket noi:
  - ToF 1 (VL53L0X): I2C0 (SDA=GP0, SCL=GP1)
  - ToF 2 (VL53L0X): I2C1 (SDA=GP2, SCL=GP3)
  - Solenoid 1     : GP10 (L298N IN1)
  - Solenoid 2     : GP11 (L298N IN3)
"""

import time
from machine import Pin, I2C

# ======================= DRIVER TOF VL53L0X (Toi gian) =======================
class VL53L0X:
    DEFAULT_ADDR = 0x29

    def __init__(self, i2c, address=DEFAULT_ADDR):
        self.i2c = i2c
        self.address = address
        self.init_sensor()

    def _write_reg(self, reg, val):
        self.i2c.writeto_mem(self.address, reg, bytes([val]))

    def _write_reg16(self, reg, val):
        self.i2c.writeto_mem(self.address, reg, bytes([(val >> 8) & 0xFF, val & 0xFF]))

    def _read_reg(self, reg):
        return self.i2c.readfrom_mem(self.address, reg, 1)[0]

    def _read_reg16(self, reg):
        data = self.i2c.readfrom_mem(self.address, reg, 2)
        return (data[0] << 8) | data[1]

    def init_sensor(self):
        # Kiem tra ket noi qua Model ID (0xEE)
        try:
            model_id = self._read_reg(0xC0)
            if model_id != 0xEE:
                raise RuntimeError("Khong phai VL53L0X (ID: 0x{:02X})".format(model_id))
        except Exception as e:
            raise RuntimeError("Loi I2C khi ket noi VL53L0X: {}".format(e))

        # Bat 2V8 I/O va standard mode
        self._write_reg(0x89, self._read_reg(0x89) | 0x01)
        self._write_reg(0x88, 0x00)

        self._write_reg(0x80, 0x01)
        self._write_reg(0xFF, 0x01)
        self._write_reg(0x00, 0x00)
        self.stop_variable = self._read_reg(0x91)
        self._write_reg(0x00, 0x01)
        self._write_reg(0xFF, 0x00)
        self._write_reg(0x80, 0x00)

        # Khoi dong che do do lien tuc (back-to-back)
        self._write_reg(0x80, 0x01)
        self._write_reg(0xFF, 0x01)
        self._write_reg(0x00, 0x00)
        self._write_reg(0x91, self.stop_variable)
        self._write_reg(0x00, 0x01)
        self._write_reg(0xFF, 0x00)
        self._write_reg(0x80, 0x00)
        self._write_reg(0x00, 0x02)  # SYSRANGE_START back-to-back

    def is_data_ready(self):
        try:
            return (self._read_reg(0x13) & 0x07) != 0
        except:
            return False

    def read_distance_mm(self):
        try:
            dist = self._read_reg16(0x14 + 10)
            self._write_reg(0x0B, 0x01)  # Clear interrupt
            return dist
        except:
            return 8190

# ======================= CAU HINH PHAN CUNG =======================
# I2C0 cho Tram 1
i2c0 = I2C(0, sda=Pin(0), scl=Pin(1), freq=400000)
# I2C1 cho Tram 2 (phan cung doc lap, tranh trung dia chi 0x29)
i2c1 = I2C(1, sda=Pin(2), scl=Pin(3), freq=400000)

sol1_pin = Pin(10, Pin.OUT, value=0)
sol2_pin = Pin(11, Pin.OUT, value=0)

try:
    led = Pin("LED", Pin.OUT)
except:
    led = Pin(25, Pin.OUT)

# ======================= DINH NGHIA TRAM =======================
class Station:
    MODE_PULSE = 0   # Hut trong khoang thoi gian roi nha
    MODE_HOLD  = 1   # Hut giu cho den khi vat di qua

    STATE_IDLE       = 0
    STATE_ACTIVE     = 1
    STATE_WAIT_CLEAR = 2

    def __init__(self, name, i2c_bus, sol_pin, mode, threshold_mm, on_time_ms):
        self.name = name
        self.i2c = i2c_bus
        self.sol_pin = sol_pin
        self.mode = mode
        self.threshold_mm = threshold_mm
        self.on_time_ms = on_time_ms

        self.sensor = None
        self.sensor_ok = False
        self.distance_mm = 8190
        self.state = self.STATE_IDLE
        self.state_start_ms = 0
        self.hit_count = 0
        self.last_retry_ms = 0
        self.total_sorted = 0

        self.init_sensor()

    def init_sensor(self):
        self.last_retry_ms = time.ticks_ms()
        try:
            self.sensor = VL53L0X(self.i2c)
            self.sensor_ok = True
            print("[{}] VL53L0X khoi tao THANH CONG".format(self.name))
        except Exception as e:
            self.sensor_ok = False
            print("[{}] LOI khoi tao VL53L0X: {}".format(self.name, e))

    def set_solenoid(self, state):
        self.sol_pin.value(1 if state else 0)
        print("[{}] Solenoid -> {} | d={} mm".format(
            self.name, "HUT (ON)" if state else "NHA (OFF)", self.distance_mm
        ))

    def update(self):
        now = time.ticks_ms()

        # 1. Xu ly khi mat ket noi cam bien
        if not self.sensor_ok:
            if self.state != self.STATE_IDLE:
                self.set_solenoid(False)
                self.state = self.STATE_IDLE
            if time.ticks_diff(now, self.last_retry_ms) > 2000:
                self.init_sensor()
            return

        # 2. Doc cam bien (non-blocking)
        if self.sensor.is_data_ready():
            d = self.sensor.read_distance_mm()
            self.distance_mm = d
            if d < self.threshold_mm and d < 8000:
                self.hit_count = min(self.hit_count + 1, 10)
            else:
                self.hit_count = 0

        object_present = self.hit_count >= 2
        object_gone = self.distance_mm >= (self.threshold_mm + 20)  # Hysteresis 20mm
        elapsed = time.ticks_diff(now, self.state_start_ms)

        # 3. May trang thai
        if self.state == self.STATE_IDLE:
            if object_present:
                print("[{}] PHAT HIEN VAT: d={} mm (< {} mm)".format(
                    self.name, self.distance_mm, self.threshold_mm
                ))
                self.set_solenoid(True)
                self.state = self.STATE_ACTIVE
                self.state_start_ms = now
                self.total_sorted += 1

        elif self.state == self.STATE_ACTIVE:
            if self.mode == self.MODE_PULSE:
                if elapsed >= self.on_time_ms:
                    self.set_solenoid(False)
                    self.state = self.STATE_WAIT_CLEAR
                    self.state_start_ms = now
            else:  # MODE_HOLD
                if object_gone:
                    print("[{}] Vat da di qua sau {} ms".format(self.name, elapsed))
                    self.set_solenoid(False)
                    self.state = self.STATE_IDLE
                    self.hit_count = 0
                elif elapsed >= self.on_time_ms:  # Bao ve qua nhiet solenoid
                    print("[{}] CANH BAO: Hut qua thoi gian an toan, nha solenoid!".format(self.name))
                    self.set_solenoid(False)
                    self.state = self.STATE_WAIT_CLEAR
                    self.state_start_ms = now

        elif self.state == self.STATE_WAIT_CLEAR:
            if object_gone:
                print("[{}] Vung do da trong -> san sang".format(self.name))
                self.state = self.STATE_IDLE
                self.hit_count = 0

# Khoi tao 2 tram doc lap
station1 = Station("TRAM1", i2c0, sol1_pin, Station.MODE_PULSE, threshold_mm=150, on_time_ms=2000)
station2 = Station("TRAM2", i2c1, sol2_pin, Station.MODE_HOLD,  threshold_mm=200, on_time_ms=5000)

print("\n===== HE THONG BANG TAI KHOI DONG THANH CONG =====")

# ======================= VONG LAP CHINH =======================
last_print = 0
last_blink = 0
led_val = 0

while True:
    # 2 tram duoc goi lien tuc khong lam chan nhau
    station1.update()
    station2.update()

    now = time.ticks_ms()

    # In log dinh ky 250ms
    if time.ticks_diff(now, last_print) >= 250:
        last_print = now
        st1_name = ["IDLE", "ACTIVE", "CLEAR"][station1.state]
        st2_name = ["IDLE", "ACTIVE", "CLEAR"][station2.state]
        print("T1: {:4d}mm [{:6s} | Total:{}]  |  T2: {:4d}mm [{:6s} | Total:{}]".format(
            station1.distance_mm, st1_name, station1.total_sorted,
            station2.distance_mm, st2_name, station2.total_sorted
        ))

    # Nhay LED he thong 1Hz
    if time.ticks_diff(now, last_blink) >= 500:
        last_blink = now
        led_val = 1 - led_val
        led.value(led_val)

