---
name: pico-conveyor
description: Làm việc với firmware băng tải phân loại Pico 2 W (C++/MicroPython, ToF, L298N). Dùng khi sửa main.cpp, đổi chân GP, debug cảm biến, COM, build hoặc nạp UF2.
---

# Băng tải phân loại — Pico 2 W

Đọc trước khi sửa: `docs/decisions.md` (quyết định), `docs/conflicts-and-bugs.md` (lỗi đã gặp), `docs/debug-cam-bien.md` (debug ToF).

## Sự thật cần nhớ
- Chân tính theo **GP**, không phải số chân vật lý. C++: ToF1 SDA/SCL GP0/GP1, ToF2 GP6/GP7, solenoid GP10/GP11.
- `test.cpp` và `main_micropython.py` dùng ToF2 GP2/GP3 (khác `main.cpp`). Đổi bản firmware phải đối chiếu dây trước.
- C++ dùng VL53L1X, MicroPython dùng VL53L0X. Không trộn driver.
- Bản C++ không đọc lệnh từ serial; gõ `status` hay `i2c.scan()` không có tác dụng.
- Không chạy `main_micropython.py` bằng Python Windows (thiếu module `machine`).

## Gate 0 trước khi sửa hoặc nạp
1. Đối chiếu chân: `Select-String -Path main.cpp,test.cpp,main_micropython.py -Pattern 'TOF2_SDA_PIN|TOF2_SCL_PIN|i2c1 = I2C'`
2. Tìm cổng COM hiện tại (số có thể đổi khi cắm lại; `monitor.bat` cố định COM5): `[System.IO.Ports.SerialPort]::GetPortNames()`
3. Chỉ mở một Serial Monitor/MicroPico tại một thời điểm; kẹt "Closing port…" thì rút/cắm lại USB.

## Build và nạp
- Build: task **Compile Project** trong VS Code (Pico SDK) → `build/BangTaiPhanLoai.uf2`.
- Nạp: giữ BOOTSEL khi cắm Pico, chép UF2 vào ổ BOOTSEL. Không nạp khi chưa được yêu cầu.
- Test logic MicroPython trên máy: `py -3.11 -m unittest discover -s tests -v`.

## Khi kết thúc việc
Ghi lỗi hoặc lệch cấu hình mới vào `docs/conflicts-and-bugs.md`, quyết định mới vào `docs/decisions.md` (D-xx kế tiếp).
