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
