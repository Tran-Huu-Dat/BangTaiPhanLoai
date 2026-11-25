# Lỗi và khác biệt cấu hình đã xác nhận

## HW-01 — Hai mẫu cách nhau 2–3 cm chỉ gạt mẫu đầu (2026-11-25)

- Base commit: `f56f416`; đã sửa trong `sorter_fsm.h`, `main.cpp`.
- Nguyên nhân: (1) `SolenoidActuator::tick()` tắt xung cũ và bật ngay xung kế tiếp trong cùng millisecond nên `changed == false`, solenoid không nhả để hồi về; (2) `on_time_ms = 500 ms` + timing budget ToF `40 ms` + `CLEAR_COUNT = 2` không kịp hồi giữa hai mẫu sát nhau.
- Sửa: thêm `min_off_time_ms` (mặc định `120 ms`) và cơ chế tách pha nhả (`has_released_`) trong `SolenoidActuator`; hạ `on_time_ms = 220 ms`, `CLEAR_COUNT = 1`, ToF `15 ms` (~66 Hz), `DETECT_DELTA_MM = 6.0`, `CLEAR_DELTA_MM = 5.0`.
- Gate 0: `g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only tests/test_sorter_fsm.cpp` (`test_back_to_back_retract_gap`).

## HW-02 — `VL53L4CD` (`0xEBAA`) kẹt cờ `i2c_error` sau soft-reset (2026-11-25)

- Base commit: `f56f416`; đã sửa trong `vl53l1x.cpp`.
- Nguyên nhân: soft-reset (`writeReg(0x0000, 0x00)`) làm slave NACK hợp lệ, nhưng cờ `i2c_error = true` không được xóa trước khi đọc trạng thái boot nên `VL53L1X::init()` trả về `false` dù chip đã boot (`0x03`).
- Sửa: xóa `i2c_error` và thông tin chẩn đoán I2C sau soft-reset và sau khi xác nhận `booted == true`.

## ENV-02 — `picotool.exe` bị Windows Device Guard chặn (2026-11-25)

- Hiện tượng: bước `POST_BUILD` của Ninja gọi `picotool.exe` thất bại với mã `4551` (App Control / Device Guard).
- Xử lý: giữ nguyên chính sách máy; sau khi `BangTaiPhanLoai.elf` liên kết xong, xuất `.bin` qua `arm-none-eabi-objcopy -Obinary` và đóng gói `.uf2` RP2350-E10 bằng Python để nạp `BOOTSEL`.

## UX-01 — Thông số Web phụ thuộc nhau & lệ thuộc mốc `H0` (2026-11-25)

- Nguyên nhân: hai ô nhập ToF cho Trạm 1/2 dùng chung điểm ranh giới `T_ML` và suy ngược từ `H0` chung trên trình duyệt, gây lỗi khi hai cảm biến gắn lệch độ cao hoặc muốn để khe đệm sai số ToF.
- Sửa (`D-14`): chuyển sang cấu hình 2 khoảng chiều cao độc lập (`L=[l_min_mm, l_max_mm]`, `M=[m_min_mm, m_max_mm]`) dùng `H0` riêng từng cảm biến; `classify_ranges()` trả `S` cho khe đệm.
- Gate 0: `tests/test_sorter_core.cpp` + `dashboard/tests/test_serial_bridge.py` (`test_station_config_api_and_serial_format`).

## SPEC-01 — Ví dụ trung vị không khớp AC (2026-11-25)

- Base commit: `f56f416`; bản sửa chưa commit.
- Nguyên nhân: SPEC-SORT §9 ghi median=50.1/h=49.9 và không phân biệt mẫu nền 99 với mẫu mép 75; AC02 lại yêu cầu h=50.
- Ảnh hưởng: test bộ lọc có thể chọn sai cửa sổ mẫu. Sửa §9 thành nền 99, bỏ mép 75, median ba mẫu còn lại=50/h=50; không sửa bản chuyển đổi nguồn.
- Gate 0: đối chiếu SORT-AC02 và SPEC-SORT §9. Test lõi phân loại mới ở `tests/test_sorter_core.cpp` chưa bao phủ bộ lọc.

## SPEC-02 — Ví dụ JSON không parse được (2026-11-25)

- Base commit `f56f416`; chưa commit. Năm block JSON của SPEC-MQTT/CALIB chứa comment hoặc nhiều object nối tiếp, trái kết quả tự kiểm bàn giao.
- Sửa: đưa chú thích ra ngoài fence, tách từng bản tin; không đổi payload. Ảnh hưởng: ví dụ copy/paste để kiểm tra giao thức.
- Gate 0: lấy từng fence json trong `docs/specs/*.md`, parse bằng `ConvertFrom-Json -ErrorAction Stop`: 7/7 đạt sau sửa.

## ENV-01 — Host C++ test bị Application Control chặn (2026-11-25)

- Base commit: `f56f416`; chưa commit. GCC cần `C:\msys64\ucrt64\bin` trong PATH để compile/link.
- Compile/link đạt nhưng Windows Application Control từ chối chạy EXE trong TEMP. `$LASTEXITCODE` giữ 0 của compiler nên không được dùng làm bằng chứng runtime pass.
- Phạm vi: kiểm chứng host mới; không thay đổi chính sách máy. Gate: chạy test trên host được phép, xác nhận tiến trình thực sự khởi chạy và kết thúc 0. Hiện chỉ syntax/warnings và 5 test Python cũ đạt.

Đối chiếu ngày 2026-11-22, mã nguồn tại commit `e181c0066717eb72ece27c525e25bb6a32bf7b2c`. Các mục dưới đây là kết quả chẩn đoán, chưa sửa firmware.

## DBG-01 — Chạy MicroPython bằng Python Windows

- Nguyên nhân: chạy `main_micropython.py` bằng Python trên máy tính; `machine` là API của MicroPython trên bo. Bo đã trả log C++ qua COM6 trong phiên kiểm tra.
- Ảnh hưởng: `ModuleNotFoundError: No module named 'machine'`; lệnh Python không chạy trên bo đang dùng firmware C++.
- Kiểm tra: đối chiếu lệnh gọi Python và `main_micropython.py:17`; đọc COM6 thấy các dòng `TRAM1: d=... TRAM2: d=...` tương ứng `main.cpp`.
- Xử lý: dùng Serial Monitor để xem C++; chỉ dùng REPL khi bo đã nạp MicroPython và driver phù hợp cảm biến. Không cài gói `machine` trên Windows để chữa lỗi này.

## DBG-02 — Chân cảm biến thứ hai khác nhau giữa các bản

- Nguyên nhân: `main.cpp` dùng SDA GP6/SCL GP7; `test.cpp` và `main_micropython.py` dùng SDA GP2/SCL GP3.
- Ảnh hưởng: đổi bản firmware mà giữ nguyên dây có thể làm trạm 2 báo lỗi dù cảm biến vẫn hoạt động.
- Gate 0: `Select-String -Path main.cpp,test.cpp,main_micropython.py -Pattern 'TOF2_SDA_PIN|TOF2_SCL_PIN|i2c1 = I2C'`.
- Xử lý: đối chiếu dây và cấu hình trước khi nạp `TestToF`; chưa đổi chân hoặc nạp firmware trong đợt nghiên cứu này.

## DBG-03 — Cổng COM trong hướng dẫn chạy cũ không trùng thiết bị

- Nguyên nhân: `monitor.bat` cố định COM5; thiết bị USB được xác nhận là COM6 trong phiên này, COM3/COM4 là Bluetooth. Số COM có thể đổi khi cắm lại.
- Ảnh hưởng: không thấy log hoặc mở sai cổng. MicroPico từng kẹt ở “Closing port…”; rút/cắm lại USB đã cho phép đọc COM6.
- Gate 0: `[System.IO.Ports.SerialPort]::GetPortNames()` và `Get-ItemProperty 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM'`.
- Xử lý: chọn cổng USB hiện tại, chỉ mở một Serial Monitor/MicroPico tại một thời điểm; xem `debug-cam-bien.md`.

## INT-01 — Tích hợp dashboard (2026-11-23)

- Đường dẫn: bản vá gốc mặc định `PEGATRON_IOT_DIR=../pegatron-iot`; trong repo này dashboard nằm ở `dashboard/`, đã đổi mặc định trong `CMakeLists.txt`.
- Lệnh điều khiển: backend khóa lệnh khi `estop` là `null`; firmware không có đầu vào E‑STOP nên thêm `PEGATRON_ESTOP_ABSENT=1` (rủi ro được người vận hành chấp nhận, xem docs/dashboard.md).
- Chưa thử trên Pico thật: chỉ có build firmware (Pico SDK 2.3.1) và 10 test backend đạt.

## DBG-04 — Danh sách Wi‑Fi mô phỏng bị hiểu là quét thật (2026-11-23)

- Nền mã: `73d363b`; bản sửa đang ở working tree, chưa commit.
- Nguyên nhân: launcher chọn `sorter-sim`; simulator trả ba SSID cố định nhưng hộp thoại ghi “Wi‑Fi của thiết bị”, không cho biết nguồn.
- Ảnh hưởng: tên mạng khác Windows; thao tác giả lập có thể bị hiểu là đã cấu hình Pico thật.
- Xử lý: khi mô phỏng, backend dùng Windows WLAN API để quét thật, trả nguồn `windows`, giao diện chỉ xem và backend từ chối kết nối giả. Thiết bị thật giữ luồng MQTT và nguồn `device`. Xóa danh sách cũ ngay khi quét lại; lỗi không dùng dữ liệu mẫu để thay thế.
- Lỗi phát hiện qua trình duyệt: lần quét Windows đầu tiên có thể chậm hơn giới hạn HTTP 15 giây, khiến giao diện báo lỗi dù backend lấy được mạng. Chuyển quét sang luồng nền, trả mã yêu cầu ngay và chờ kết quả tối đa 45 giây trên giao diện.
- Gate 0: `cd dashboard; .\.venv\Scripts\python.exe -m unittest discover -s tests -v` (14 test đạt); `node --check dashboard/static/wifi.js` từ gốc repo. Test bao gồm nguồn, offline simulator, chặn kết nối giả, lỗi quyền, giữ luồng MQTT máy thật, phản hồi ngay khi Windows quét chậm, gộp SSID, Unicode và giải phóng bộ nhớ native sau timeout.
- Kiểm tra thực tế: trình duyệt tại `http://127.0.0.1:8000` hiển thị năm mạng thật, quét lại thành công, xóa danh sách cũ và giữ chế độ chỉ xem. Chưa xác nhận quét/chuyển mạng trên Pico. Không suy ra phần cứng đã kết nối từ kết quả quét của Windows.
