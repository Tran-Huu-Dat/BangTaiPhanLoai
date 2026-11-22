# BangTaiPhanLoai

Băng tải phân loại dùng Raspberry Pi Pico 2 W và hai solenoid qua L298N.
Bản C++ (`main.cpp`) dùng cảm biến VL53L1X; bản MicroPython
(`main_micropython.py`) dùng VL53L0X. Chọn bản đúng với cảm biến thực tế.

| Trạm | Phôi | Khoảng cách từ cảm biến tới mặt phôi | Thời gian kích |
| --- | --- | --- | --- |
| 1 | M | 65–75 mm, gồm hai đầu mút | 500 ms rồi nhả |
| 2 | L | 50–60 mm, gồm hai đầu mút | 500 ms rồi nhả |

Size S nằm ngoài hai khoảng nhận nên đi qua cả hai trạm. Mặt băng tải không
có phôi đo được 85–90 mm nên không kích solenoid. Đây là khoảng cách đo,
không phải chiều cao phôi.

Mỗi trạm cần 2 mẫu đo liên tiếp trong khoảng nhận mới kích, không có trễ
di chuyển. Sau khi kích, trạm phải nhận 2 mẫu hợp lệ liên tiếp từ 80 mm
trở lên (thấy lại mặt băng tải) mới sẵn sàng nhận phôi tiếp theo. Mẫu lỗi
không mở khóa lượt đẩy mới. Hai trạm hoạt động độc lập.

## Mô phỏng đo vận tốc bằng hai ToF

Biên dịch trên máy tính bằng g++ hỗ trợ C++17 rồi chạy trong PowerShell:

```powershell
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++17 belt_speed.cpp simulate_belt_speed.cpp -o build/simulate_belt_speed.exe
.\build\simulate_belt_speed.exe --once
```

Bỏ `--once` để theo dõi liên tục, nhấn Ctrl+C để dừng. Bản demo lấy mẫu ToF mỗi
20 ms, lần lượt chạy ở 200 và 220 mm/s. Trạm 1 đẩy M, trạm 2 đẩy L, S đi
thẳng. Các khoảng băng trống cuối mỗi lượt cho thấy nguồn vận tốc thay đổi.

`BeltSpeedMonitor` trong `belt_speed.h` và `belt_speed.cpp` nhận khoảng cách
ToF theo mm ở mỗi trạm. Ngưỡng hiện diện tách phôi khỏi nền băng. Module lưu
thời điểm cạnh vào đầu tiên, bỏ mẫu mép và lấy trung vị 3–5 mẫu chiều cao
`(H₀ − d − c) / k` để phân cỡ. Hai mẫu nền xác nhận cạnh ra, lấy thời điểm
mẫu nền đầu tiên. Phôi cần che đủ lâu và để hở nền giữa hai lượt.

| Nguồn | Khi cập nhật | Cách tính vận tốc (mm/s) |
| --- | --- | --- |
| Cặp ToF | S hoặc L ra khỏi vùng đo trạm 2 | Trung bình của `L₁₂ / Δt_vào` và `L₁₂ / Δt_ra` giữa hai trạm |
| Thời gian che ToF1 | S, M hoặc L rời ToF1 | `(chiều dài phôi + vết đo hiệu dụng theo cỡ) / thời gian che` |
| Mô hình động cơ | Mỗi 100 ms có dữ liệu PWM và điện áp | `a × PWM(%) × V_s / V_nom + b` |

Thời gian trong bảng tính bằng giây. Các cặp ToF được ghép theo thứ tự
và kiểm tra cùng cỡ, với giả định phôi không vượt nhau, mỗi phôi được thấy
ở cả hai trạm dự kiến. Bỏ sót một phôi có thể ghép lệch hai phôi cùng cỡ.
Mốc quá thời gian hành trình cấu hình sẽ hết hạn.

Khởi tạo một `BeltSpeedMonitor monitor(config);` rồi gọi
`monitor.update(now_ms, tof1_mm, tof2_mm, motor)` theo từng vòng lặp.
Truyền `std::nullopt` cho đầu vào chưa có mẫu mới và `0` cho khoảng cách ToF lỗi.
Kết quả trả về chứa phôi từng trạm và các phép đo mới được chấp nhận.
`speedMmPerSecond()` cho vận tốc đã lọc, dùng trọng số 0,30 cho cặp ToF,
0,10 cho thời gian che và 0,02 cho mô hình. Module loại mẫu lệch quá ±20%
so với ước lượng đang có. `deviation()` so số đo ToF với mô hình, báo cảnh
báo nếu lệch trên 5% liên tục 30 giây, báo lỗi khi lệch trên 10%.

Mỗi giây, mô phỏng in vận tốc đã lọc, mô hình, tuổi cặp ToF và nguồn ưu tiên
còn mới. Sau 30 giây không có cặp mới, nguồn hiển thị chuyển sang thời gian
che nếu vẫn có phôi M, hoặc sang mô hình nếu băng trống. Khi mọi nguồn hết
hạn, `speedMmPerSecond()` trả về `std::nullopt`.

Bộ tham số trong `simulate_belt_speed.cpp` là số liệu giả lập, gồm khoảng
cách hai trạm 300 mm, hệ số ToF, chiều dài phôi, vết đo và hệ số PWM.
Mô hình động cơ cố ý cao hơn vận tốc thực mô phỏng 2% để quan sát bộ lọc.
Khi dùng phần cứng, thay các tham số bằng số đo hiệu chuẩn của băng thực.

Chạy kiểm thử module bằng `g++ -std=c++17 belt_speed.cpp tests/test_belt_speed.cpp -o build/test_belt_speed.exe`
rồi chạy `.\build\test_belt_speed.exe`.

## Kết nối L298N

- GP10 → IN1; solenoid trạm 1 nối giữa OUT1 và OUT2.
- GP11 → IN3; solenoid trạm 2 nối giữa OUT3 và OUT4.
- IN2 và IN4 giữ LOW (nối GND); ENA và ENB bật (giữ jumper enable nếu module có).
- Nối chung GND giữa Pico, L298N và nguồn solenoid; cấp nguồn công suất
  phù hợp với solenoid vào L298N.

Theo cách nối này, GP10/GP11 HIGH cấp điện cho solenoid, LOW ngắt kích.
Cách chuyển động khi cấp điện phụ thuộc cơ cấu solenoid thực tế.
Tham khảo [datasheet L298 của ST](https://www.st.com/resource/en/datasheet/l298.pdf).

## Build và nạp

Build bằng task **Compile Project** của VS Code với Raspberry Pi Pico SDK.
Firmware được tạo tại `build/BangTaiPhanLoai.uf2`; bản sao ở thư mục gốc
là `BangTaiPhanLoai.uf2`. Giữ BOOTSEL khi cắm Pico rồi chép firmware mới
vào ổ BOOTSEL để áp dụng cấu hình.

Thử phôi S, M, L thực tế để xác nhận các khoảng đo và hành trình đẩy 500 ms.
Có thể kiểm tra mô phỏng logic MicroPython trên máy tính bằng
`py -3.11 -m unittest discover -s tests -v` (không cần gắn phần cứng).
