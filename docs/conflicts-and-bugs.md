# Lỗi và khác biệt cấu hình đã xác nhận

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
