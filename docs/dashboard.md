# Dashboard điều khiển băng tải

Dashboard là trang web chạy trên laptop, theo dõi và điều khiển băng tải qua Wi‑Fi. Mã nguồn nằm ở thư mục `dashboard/`, firmware Pico nằm ở `main.cpp` và `dashboard/firmware/`.

## 1. Cơ chế hoạt động

```
Trình duyệt ──HTTP──► FastAPI (app.py) ──MQTT──► Mosquitto ◄──MQTT── Pico 2 W
 localhost:8000        + SQLite lưu lịch sử      laptop:1883        (lõi 1: mạng)
                                                                     │ hàng đợi
                                                                   Pico lõi 0: ToF + solenoid
```

- **Pico lõi 0** chạy thuật toán phân loại cũ (đọc ToF, kích solenoid). Lõi này là nơi duy nhất đụng vào GPIO.
- **Pico lõi 1** chạy Wi‑Fi và MQTT. Nó nhận một bản sao trạng thái 100 ms/lần từ lõi 0 qua hàng đợi, gửi lên broker mỗi giây (và ngay khi chế độ/lỗi đổi).
- **Broker Mosquitto** chuyển tin giữa Pico và backend. ACL giới hạn mỗi tài khoản chỉ được đọc/ghi đúng topic của nó.
- **Backend FastAPI** kiểm tra từng gói tin theo `dashboard/protocol.py`, lưu vào SQLite (`dashboard/data/<device>.sqlite3`), phục vụ giao diện và chuyển lệnh của bạn xuống Pico.
- **Mạng chỉ là kênh đề nghị.** Pico tự kiểm tra lại mọi lệnh trước khi làm. Mất Wi‑Fi không làm băng tải dừng hoạt động đang chạy; mất Wi‑Fi cũng không thể bắt Pico kích solenoid.

### Topic MQTT (`pegatron/v1/pico-01/…`)

| Topic | Chiều | QoS | Nội dung |
| --- | --- | --- | --- |
| `availability` | Pico → web | 1, retain | `online`/`offline` kèm `link_id`. Last Will tự gửi `offline` khi mất kết nối |
| `telemetry` | Pico → web | 0 | Chế độ, khoảng cách ToF, số lần gạt, quyền điều khiển từng nút |
| `events` | Pico → web | 1 | Lỗi phát sinh/hết, đổi chế độ |
| `command` | web → Pico | 1 | Lệnh điều khiển |
| `wifi/request`, `wifi/result` | web ↔ Pico | 1 | Quét và đổi Wi‑Fi (mục 4) |
| `ack` | Pico → web | 1 | Kết quả lệnh: `executed` hoặc `rejected` kèm lý do |

Định dạng chính xác của từng trường: `dashboard/protocol.py`.

## 2. Điều khiển được gì

| Nút | Lệnh | Pico làm gì | Từ chối khi |
| --- | --- | --- | --- |
| Bật phân loại | `ENABLE_SORTING` | Cho phép kích solenoid theo logic cũ | `ALREADY_SORTING` |
| Tạm dừng phân loại | `PAUSE_SORTING` | Nhả solenoid ngay, xóa hàng đợi trễ, vẫn đo ToF | `ALREADY_PAUSED` |
| Thử bộ đẩy 1/2 | `TEST_PUSHER` | Một xung 500 ms như bình thường | đang phân loại (`SORTING_ACTIVE`), ToF lỗi, trạm bận |
| Hiệu chỉnh ToF | `CALIBRATE_TOF` | Chưa hỗ trợ trên firmware C++ | luôn `NOT_SUPPORTED` |

Pico **bật phân loại ngay khi khởi động** như firmware gốc. Tạm dừng chỉ chặn việc kích mới, không tắt nguồn băng tải. Muốn thử bộ đẩy phải tạm dừng trước.

### Các lớp kiểm tra một lệnh đi qua

1. Trình duyệt → backend: chỉ nhận cùng origin, host `localhost`/`127.0.0.1`.
2. Backend: telemetry còn mới (≤ 3 s), không bị khóa, quyền nút đang là `OK`, không có lệnh cùng loại đang chờ.
3. Broker: chỉ `dashboard` được ghi `command`, chỉ `pico-01` được đọc.
4. Pico lõi mạng: đúng `link_id` của phiên MQTT hiện tại, `control_epoch` khớp, lệnh không cũ quá 5 s theo uptime Pico, không trùng `request_id` đã xử lý.
5. Pico lõi điều khiển: kiểm tra lại quyền theo trạng thái thật rồi mới làm, sau đó gửi `ack` kèm chế độ đọc lại sau khi làm.

Nếu `ack` không về trong 5 s, lệnh hiển thị là không xác nhận. Đừng bấm lặp; xem trạng thái thật trên băng tải/Serial.

## 3. Kết nối từ đầu

### 3.1 Cài đặt một lần

- Python 3.12, Mosquitto (cung cấp `mosquitto` và `mosquitto_passwd`).
- Pico SDK 2.3.1 + toolchain ARM (VS Code Pico extension đã cài sẵn ở `~/.pico-sdk`).

```powershell
cd dashboard
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

### 3.2 Broker

Tạo tài khoản (lần đầu dùng `-c`, các lần sau bỏ `-c`):

```powershell
mosquitto_passwd -c broker/passwords dashboard
mosquitto_passwd broker/passwords pico-01
```

Sửa `broker/mosquitto.conf`: đổi `listener 1883 127.0.0.1` thành `listener 1883 <IPv4 laptop>` (IP của laptop trên Wi‑Fi 2,4 GHz mà Pico dùng). Mở Windows Firewall cho TCP 1883 chỉ với subnet đó. Chạy từ thư mục `dashboard`:

```powershell
mosquitto -c broker/mosquitto.conf -v
```

`broker/acl` đã mở sẵn quyền `command`/`ack` cho `pico-01`.

### 3.3 Build và nạp firmware

Từ thư mục gốc của repo, đặt thông tin Wi‑Fi trong phiên PowerShell (không ghi vào mã, nhưng sẽ nằm trong file build nên không chia sẻ file `.uf2`):

```powershell
$env:PEGATRON_WIFI_SSID = '<tên Wi-Fi>'
$env:PEGATRON_WIFI_PASSWORD = '<mật khẩu Wi-Fi>'
$env:PEGATRON_BROKER_IPV4 = '<IPv4 laptop>'
$env:PEGATRON_MQTT_PASSWORD = '<mật khẩu pico-01 đã tạo ở 3.2>'
```

Chạy task **Compile Project** của VS Code (hoặc `cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w` rồi `cmake --build build`). Giữ BOOTSEL, cắm Pico, chép `build/BangTaiPhanLoai.uf2` vào ổ đĩa mới. Nếu `PEGATRON_WIFI_SSID` để trống, firmware chạy như bản cũ, không dùng mạng.

Giá trị mặt băng trống (để hiện chiều cao phôi) tùy chọn: thêm `-DPEGATRON_REFERENCE_1_MM=<mm> -DPEGATRON_REFERENCE_2_MM=<mm>`. Mặc định 0 là chưa xác nhận, giao diện hiện khoảng cách ToF.

### 3.4 Chạy backend và mở web

```powershell
cd dashboard
$env:PEGATRON_DEVICE_ID = 'pico-01'
$env:PEGATRON_MQTT_HOST = '<IPv4 laptop>'
$env:PEGATRON_MQTT_USER = 'dashboard'
$env:PEGATRON_MQTT_PASSWORD = '<mật khẩu dashboard>'
$env:PEGATRON_HARDWARE_CONTROL = '1'   # cho phép gửi lệnh tới máy thật
$env:PEGATRON_ESTOP_ABSENT = '1'       # máy này không có đầu vào E-STOP
.\.venv\Scripts\python.exe -m uvicorn app:app --host 127.0.0.1 --port 8000
```

Mở <http://127.0.0.1:8000>. Dấu hiệu nối thành công: LED trên Pico nháy 0,5 s, trang hiện **Trực tuyến**, số liệu ToF cập nhật mỗi giây.

| Biến môi trường | Ý nghĩa |
| --- | --- |
| `PEGATRON_DEVICE_ID` | `pico-01` cho máy thật, `sorter-sim` (mặc định) cho giả lập |
| `PEGATRON_MQTT_HOST` / `PORT` / `USER` / `PASSWORD` | Địa chỉ và tài khoản broker |
| `PEGATRON_HARDWARE_CONTROL` | `1` mới cho gửi lệnh tới máy thật; mặc định chỉ giám sát |
| `PEGATRON_ESTOP_ABSENT` | `1` chấp nhận trạng thái E‑STOP “không có” (máy chưa có nút dừng khẩn) |
| `PEGATRON_DB` | Tệp SQLite riêng |

> **An toàn:** `PEGATRON_ESTOP_ABSENT=1` nghĩa là dashboard không có tín hiệu dừng khẩn nào để kiểm tra. Hãy giữ công tắc ngắt nguồn solenoid ngoài máy trong tầm tay khi thử lệnh `TEST_PUSHER`.

### 3.5 Thử không cần phần cứng

```powershell
.\.venv\Scripts\python.exe broker/dev_broker.py          # cửa sổ 1
.\.venv\Scripts\python.exe -m uvicorn app:app --host 127.0.0.1 --port 8000   # cửa sổ 2
.\.venv\Scripts\python.exe simulator.py --interactive    # cửa sổ 3
```

Giả lập dùng `PEGATRON_DEVICE_ID=sorter-sim` (mặc định) và có nhãn **GIẢ LẬP** trên web. Kiểm thử tự động: `.\.venv\Scripts\python.exe -m unittest discover -s tests`.

## 4. Đổi Wi‑Fi của Pico từ web

Bấm biểu tượng Wi‑Fi ở góc phải thanh đầu trang: web nhờ Pico quét mạng, hiện danh sách theo độ mạnh tín hiệu, chọn mạng, nhập mật khẩu rồi bấm **Kết nối**. Mạng mở (không khóa) không cần mật khẩu.

Luồng chạy qua MQTT, topic `pegatron/v1/pico-01/wifi/request` (web → Pico) và `wifi/result` (Pico → web):

1. Quét: Pico quét ngay cả khi đang online, trả tối đa 12 mạng mạnh nhất, mỗi tên một dòng.
2. Kết nối: Pico nhớ mạng cũ, rời mạng, vào mạng mới rồi nối lại MQTT. Trong lúc này thiết bị ngoại tuyến vài giây.
3. Chỉ khi **MQTT online trên mạng mới** (tối đa 25 giây) Pico mới lưu tên mạng và mật khẩu vào flash (sector cuối) rồi báo thành công. Từ lần khởi động sau Pico dùng mạng đã lưu thay cho mạng ghi lúc build.
4. Nếu sai mật khẩu (`WIFI_FAILED`) hoặc vào được mạng nhưng không thấy broker (`BROKER_UNREACHABLE`), Pico quay về mạng cũ và báo lỗi lên web. Flash không bị ghi.

**Điều kiện:**
- Pico phải đang online để nhận yêu cầu. Lần đầu vẫn cần build firmware với `PEGATRON_WIFI_SSID` đúng một mạng nào đó (mục 3.3).
- Laptop phải ở **cùng mạng mới** và broker phải nghe trên IP laptop trong mạng đó, nếu không Pico không thấy broker. IP laptop có thể khác giữa các mạng, khi đó cần sửa `PEGATRON_BROKER_IPV4` rồi build lại (IP broker chưa đổi được từ web).
- Chỉ WPA/WPA2‑PSK và mạng mở. Không hỗ trợ WPA3‑only hoặc mạng doanh nghiệp.
- Mật khẩu đi qua MQTT không mã hóa trong LAN và lưu nguyên văn trong flash Pico. Backend không ghi mật khẩu vào SQLite hay log.
- Lúc lưu, flash tạm dừng lõi điều khiển vài chục ms. Nên tạm dừng phân loại trước khi đổi mạng.
- Khi chạy giả lập, nút Wi‑Fi dùng mạng giả. Mật khẩu `wrong-pass` luôn báo lỗi để thử giao diện.

## 5. Dùng dashboard

1. Bắt đầu một **buổi chạy** (đặt tên) để số S/M/L và lịch sử được ghi theo buổi.
2. **Bật phân loại**. Khi phôi M/L bị gạt, số lần gạt của trạm tăng.
3. **Tạm dừng phân loại** để dừng gạt; tải CSV của buổi chạy khi xong.
4. Quy ước đếm: phôi S không bị gạt ở trạm nào, bản firmware hiện chưa có cảm biến khay nên cột khay S/M/L hiện “chưa xác nhận”. Số trạm 1/2 là số lần kích solenoid.

## 6. Xử lý sự cố

| Triệu chứng | Nguyên nhân thường gặp |
| --- | --- |
| Web báo **Ngoại tuyến**, LED Pico nháy | Sai SSID/mật khẩu, laptop và Pico khác mạng, Firewall chặn 1883, broker vẫn nghe `127.0.0.1` |
| Pico online nhưng nút xám `MONITOR_ONLY` | Chưa đặt `PEGATRON_HARDWARE_CONTROL=1` |
| Nút xám `ESTOP_UNKNOWN` | Chưa đặt `PEGATRON_ESTOP_ABSENT=1` |
| Lệnh bị `STALE_COMMAND` | Lệnh đến Pico sau hơn 5 s (theo uptime Pico) vì mạng trễ; bấm lại |
| Lệnh `LINK_MISMATCH` | Pico vừa nối lại MQTT; đợi telemetry mới rồi bấm lại |
| Không có `ack`, lệnh không xác nhận | Pico chưa subscribe `command` (ACL sai: kiểm tra `broker/acl` và khởi động lại Mosquitto) |
| Đổi Wi‑Fi báo `BROKER_UNREACHABLE` | Laptop chưa ở mạng mới, hoặc broker chưa nghe trên IP laptop của mạng đó |
| Không thấy khoảng cách ToF | Xem [debug-cam-bien.md](debug-cam-bien.md) |

## 7. Giới hạn đã biết

- Firmware chưa có đầu vào E‑STOP, HOME hoặc cảm biến khay, các trường này hiện `null`.
- Chưa có hiệu chỉnh ToF từ web; mặt băng trống đặt lúc build.
- MQTT chạy TCP thuần trong mạng LAN riêng, mật khẩu nằm trong file firmware. Không dùng trên mạng công cộng.
- Khoảng 100 ms trễ giữa trạng thái thật và telemetry; `ack` mang chế độ đọc lại ngay sau khi làm.
- Chưa thử trên phần cứng thật trong phiên này; build đã qua, logic lệnh chưa chạy trên Pico.
