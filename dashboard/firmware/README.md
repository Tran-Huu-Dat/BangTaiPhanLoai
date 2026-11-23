# Tích hợp Pico 2 W với dashboard

Bản vá `BangTaiPhanLoai.patch` (đã áp vào `main.cpp`/`CMakeLists.txt` của repo này; chỉ là bản gốc chỉ-giám-sát, không gồm phần nhận lệnh) áp dụng lên commit `dccb6b9a135ae1bd279986946bd68fcdd159f9a8` của [BangTaiPhanLoai](https://github.com/Tran-Huu-Dat/BangTaiPhanLoai/tree/dccb6b9a135ae1bd279986946bd68fcdd159f9a8). Bản C++ dùng hai VL53L1X trên I²C0/I²C1 và ngõ ra solenoid GP10/GP11. Bản vá giữ thuật toán và tham số hiện có. Tác vụ mạng chạy trên lõi 1 và đọc bản sao trạng thái qua hàng đợi bốn phần tử. Đếm nhịp gạt tại lệnh kích GPIO, riêng với số phát hiện vật và số xác nhận vào khay.

Repo này không cung cấp đầu vào HOME, E‑STOP, cảm biến khay hoặc mạch cho phép điều khiển tại máy trong firmware C++. Các trường tương ứng hiện chưa xác nhận. Khi chưa đo mốc mặt băng, giao diện hiển thị khoảng cách ToF. Chu kỳ đo 40 ms và thời lượng gạt 2 giây/giới hạn 5 giây trong repo là cấu hình phần cứng hiện có. Các giá trị này chưa qua kiểm chứng theo mốc 200/250 mm/s của thuyết minh.

## Chuẩn bị và build bản giám sát

Cài Pico SDK 2.3.1 có các submodule CYW43/lwIP, CMake, Ninja và toolchain ARM. Bản sao BangTaiPhanLoai trong thư mục pegatron đã có nhánh iot-monitor và bản vá. Vào thư mục đó và chạy từ dòng cấu hình PICO_SDK_PATH. Nếu cần tạo bản sao mới, thực hiện đầy đủ các lệnh dưới đây tại thư mục chứa cả `pegatron-iot` và bản sao repo phần cứng:

```powershell
git clone https://github.com/Tran-Huu-Dat/BangTaiPhanLoai.git
Set-Location BangTaiPhanLoai
git checkout -b iot-monitor dccb6b9a135ae1bd279986946bd68fcdd159f9a8
git apply --check ../pegatron-iot/firmware/BangTaiPhanLoai.patch
git apply ../pegatron-iot/firmware/BangTaiPhanLoai.patch

$env:PICO_SDK_PATH = '<thư mục Pico SDK>'
$env:PEGATRON_WIFI_SSID = '<SSID Wi-Fi demo>'
$env:PEGATRON_WIFI_PASSWORD = '<mật khẩu Wi-Fi>'
$env:PEGATRON_BROKER_IPV4 = '<IPv4 laptop>'
$env:PEGATRON_MQTT_PASSWORD = '<mật khẩu broker cho pico-01>'
cmake -S . -B build-iot -G Ninja -DPICO_BOARD=pico2_w
cmake --build build-iot
```

Nếu repo phần cứng nằm chỗ khác, truyền thêm `-DPEGATRON_IOT_DIR=<đường dẫn pegatron-iot>`. Khi đã đo mặt băng trống ở từng trạm, có thể truyền `-DPEGATRON_REFERENCE_1_MM=<mm>` và `-DPEGATRON_REFERENCE_2_MM=<mm>` trong bước configure. Giá trị mặc định 0 giữ chiều cao ở trạng thái chưa xác nhận. Mật khẩu nằm trong tệp header được tạo ở thư mục build và có thể đọc được từ file firmware. Giữ các tệp đó trong nhóm vận hành.

File đầu ra là `build-iot/BangTaiPhanLoai.uf2`. Lưu bản UF2 đang dùng để khôi phục. Thử bản mới trên bàn với ngõ ra công suất tách tải, sau đó đối chiếu khoảng cách và nhịp gạt với log USB, kiểm tra tắt/bật broker rồi mới ghép lên máy.

## MQTT v1

Topic gốc là `pegatron/v1/pico-01/`. Định dạng chính xác, đơn vị và trường bắt buộc được định nghĩa trong `../protocol.py`. Thiết bị phát `availability` QoS 1 retain, đăng ký Last Will offline cùng `link_id`. Telemetry dùng QoS 0 không retain, gửi khoảng một lần mỗi giây và khi chế độ/lỗi đổi. Các sự kiện dùng QoS 1 không retain. `boot_id` thay đổi khi Pico khởi động và `link_id` thay đổi khi MQTT kết nối lại.

Firmware nhận lệnh `ENABLE_SORTING`, `PAUSE_SORTING` và `TEST_PUSHER` trên `command`, xác thực `link_id`, `control_epoch`, tuổi lệnh (≤ 5 s theo uptime) và chống lặp `request_id` ở lõi mạng, rồi chuyển qua hàng đợi sang lõi điều khiển. Lõi điều khiển kiểm tra lại quyền theo trạng thái thật, tác động, rồi gửi `ack` kèm chế độ đọc lại. `CALIBRATE_TOF` trả `NOT_SUPPORTED`. Backend cần `PEGATRON_HARDWARE_CONTROL=1` và, vì firmware chưa có đầu vào E‑STOP, `PEGATRON_ESTOP_ABSENT=1`. Xem hướng dẫn đầy đủ ở [docs/dashboard.md](../../docs/dashboard.md).
