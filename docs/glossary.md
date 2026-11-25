# Thuật ngữ & Ký hiệu — Băng tải phân loại sản phẩm Pico 2 + IoT Dashboard

> Nguồn chuẩn: [`phan-tich-thiet-ke-he-thong.md`](phan-tich-thiet-ke-he-thong.md) (§9.1, §10) và hiện trạng firmware/dashboard (`decisions.md`, `dashboard/protocol.py`).

## 1. Ký hiệu vật lý, đo lường & điều khiển

| Ký hiệu | Ý nghĩa | Đơn vị | Nguồn / Cách lấy |
| --- | --- | --- | --- |
| $v$, $\hat{v}$ | Tốc độ băng tải (danh định / ước lượng online) | mm/s | Tính từ động cơ, khớp PWM (C3), lọc trượt online (§10.1, §10.5) |
| $\hat{v}_{12}$ | Tốc độ ước lượng từ cặp ToF1 → ToF2 | mm/s | Trung bình cạnh vào/ra với khoảng cách $L_{12}$ (§10.1 ①) |
| $\hat{v}_{occ}$ | Tốc độ ước lượng từ thời gian che ToF1 | mm/s | $(\ell_{p,cls} + D_{eff,cls}) / T_{occ}$ (§10.1 ②) |
| $v_{model}$ | Tốc độ ước lượng từ mô hình PWM + điện áp nguồn $V_s$ | mm/s | $a \cdot (u \cdot V_s / V_{nom}) + b$ (§10.1 ③) |
| $v_{emf}$ | Tốc độ ước lượng từ sức phản điện động (tùy chọn INA219) | mm/s | $k_v (V_m - I_m R_a)$ (§10.1 ④) |
| $\ell_p, w_p, h$ | Chiều dài (theo chiều băng), rộng, cao sản phẩm | mm | Thể lệ cuộc thi; $h$ đo bằng ToF (§9.1, §9.4) |
| $H_0$ | Khoảng cách từ cảm biến ToF tới mặt băng trống | mm | Hiệu chuẩn bước C1, tự bù khi trôi $\le 2\text{ mm}$ (§10.3, §10.5) |
| $\sigma_0, \sigma$ | Độ lệch chuẩn nhiễu nền ToF (lúc C1 / đang chạy) | mm | C1 (200 mẫu) và cửa sổ trượt 200 mẫu khi `RUNNING` (§10.3, §10.5) |
| $d$ | Khoảng cách thô đo được từ ToF tới mặt trên vật/băng | mm | Thanh ghi VL53L0X/VL53L1X |
| $k, c$ | Hệ số góc & bù lệch tuyến tính hóa ToF từng trạm | –, mm | Khớp tuyến tính 3 khối chuẩn ở bước C2: $h = ((H_0 - d) - c)/k$ |
| $T_{SM}, T_{ML}$ | Ngưỡng phân loại chiều cao giữa cỡ S–M và M–L | mm | Điểm giữa 2 cỡ kề nhau ở bước C5, cách biên $\ge 3\sigma_{med}$ |
| $\theta$ | Góc nhìn (FoV) của cảm biến ToF | ° | VL53L0X $\approx 25^\circ$; VL53L1X $\approx 27^\circ$ (thu hẹp bằng ROI) |
| $D_{spot}$ | Đường kính vết đo ToF trên mặt sản phẩm | mm | $2(H_0 - h)\tan(\theta/2)$ (§9.4) |
| $D_{eff,cls}$ | Độ dài vết đo hiệu dụng theo từng cỡ sản phẩm | mm | Đo thực nghiệm ở bước C3: $v_{ref} T_{occ} - \ell_{cls}$ |
| $T_b$ | Timing budget — thời gian một lần đo của ToF | ms | Cấu hình firmware, thường 20–33 ms |
| $N$ | Số mẫu liên tiếp dùng để lấy trung vị chiều cao | mẫu | Cấu hình (thường 3–5 mẫu) |
| $L_p$ ($L_{p1}, L_{p2}$) | Khoảng cách từ trục cảm biến ToF tới trục bàn đẩy cùng trạm | mm | Đo cơ khí khung máy ở bước S0 |
| $L_{12}$ | Khoảng cách từ trục ToF1 tới trục ToF2 dọc băng | mm | Đo cơ khí khung máy ở bước S0 |
| $e_{det}$ | Độ lệch vị trí mép trước sản phẩm lúc phát hiện so với trục ToF | mm | Khởi đầu $\approx D_{spot}/2$, tinh chỉnh ở bước C6 |
| $b, B$ | Bề rộng bàn đẩy (theo chiều băng) và bề rộng mặt băng | mm | Thiết kế cơ khí (S0) |
| $t_0, t_1$ | Thời điểm phát hiện cạnh vào / cạnh ra của sản phẩm dưới ToF | ms/µs | Neo mọi phép hẹn giờ vào $t_0$ để không cộng dồn trễ xử lý |
| $t_{dec}$ | Thời gian ra quyết định từ $t_0$ | ms | $(N+1)T_b + t_{I2C} + t_{proc}$ (§9.4) |
| $t_{fire}, t_d$ | Thời điểm kích solenoid và độ trễ kích tính từ $t_0$ | ms | $t_{fire} = t_0 + t_d$; $t_d = (L_p + e_{det} + \ell_p/2)/\hat{v} - t_{act}$ |
| $t_{act}, t_{push}, t_{ret}$ | Thời gian trễ tác động, đẩy hết hành trình, và hồi về của solenoid | ms | Đo bằng video 240 fps ở bước C4 |
| $t_{on}, t_{dwell}$ | Thời gian cấp điện solenoid và thời gian giữ thêm cuối hành trình | ms | $t_{on}$ cấu hình (bị firmware chặn trên để chống cháy cuộn dây) |
| $D_{rated}$ | Hệ số làm việc nhiệt cho phép (duty cycle) của solenoid | % | Datasheet solenoid; giám sát trên cửa sổ trượt 60 s (§9.7, §10.5) |
| $p, g, p_{min}$ | Bước đầu–đầu giữa 2 sản phẩm ($p = \ell_p + g$), khe hở, bước tối thiểu | mm | Đảm bảo ToF thấy mặt băng và solenoid kịp hồi (§9.8) |
| $Q, Q_{st,max}$ | Năng suất dây chuyền và giới hạn nhịp đẩy nhiệt của trạm | sp/phút | $Q = 60v/p$; $Q_{st,max} = 60\,000\,D_{rated}/t_{on}[\text{ms}]$ |
| $V_s, V_{nom}$ | Điện áp nguồn động cơ thực đo qua ADC (GP27) và danh định | V | Dùng bù duty PWM và tính $v_{model}$ (§10.1 ③) |
| $\alpha_{src}$ | Trọng số bộ lọc trượt hợp nhất tốc độ theo độ tin cậy nguồn | – | $\alpha_1=0{,}3$ (cặp ToF), $\alpha_2=0{,}1$ (che ToF1), $\alpha_3=0{,}02$ (mô hình) |

## 2. Thuật ngữ phần cứng, firmware & IoT

| Thuật ngữ | Định nghĩa trong dự án |
| --- | --- |
| **Core 0 / Core 1 (RP2350)** | Hai lõi Cortex-M33 của Pico 2 W. Core 0 giữ toàn bộ vòng điều khiển thời gian thực, GPIO, alarm timer; Core 1 giữ Wi-Fi, MQTT, NTP, bộ đệm vòng và lưu flash (chỉ ghi flash khi `IDLE`). |
| **Hardware FIFO (RP2350)** | Hàng đợi phần cứng liên lõi dùng để Core 0 và Core 1 trao đổi bản sao trạng thái, sự kiện, cấu hình và lệnh mà không khóa vòng quét 1 ms. |
| **XSHUT** | Chân tắt/mở nguồn logic của cảm biến ToF VL53L0X/VL53L1X; dùng khi chung bus I²C0 để bật lần lượt và gán lại địa chỉ mặc định `0x29` thành `0x30` (ToF1) và `0x31` (ToF2). |
| **E-STOP NC** | Nút dừng khẩn cấp tiếp điểm thường đóng (Normally Closed) mắc nối tiếp trực tiếp vào cuộn rơ-le/contactor nguồn động lực (ngắt $\le 100\text{ ms}$ độc lập với MCU); Pico chỉ đọc trạng thái qua opto cách ly (`GP20`). |
| **Watchdog phần cứng** | Bộ đếm giám sát trên RP2350 (reset $\le 1\text{ s}$ khi treo về trạng thái an toàn), chỉ được cho ăn khi cả Core 0 và Core 1 còn báo sống. |
| **LWT (Last Will and Testament)** | Bản tin MQTT đăng ký trước tại broker để tự phát `offline` trên topic `status` (QoS 1, retain) khi Pico mất kết nối đột ngột. |
| **Retained message** | Bản tin MQTT được broker giữ lại phiên bản mới nhất (`status`, `config/reported`, `config/desired`, `calib/result`) để thuê bao mới nhận ngay khi kết nối. |
| **QoS 0 / QoS 1** | Mức đảm bảo giao nhận MQTT: QoS 0 (tối đa 1 lần, dùng cho `telemetry` và `calib/progress`), QoS 1 (ít nhất 1 lần, dùng cho `event/item`, `alarm`, `cmd`, `config/*`). |
| **`seq` & Chống trùng** | Số thứ tự tăng đơn điệu trên mỗi `device_id`; kết hợp ràng buộc `UNIQUE(device_id, seq)` ở SQLite/PostgreSQL để bù gói QoS 1 khi mất mạng không tạo bản ghi trùng. |
| **SQLite WAL** | Chế độ Write-Ahead Logging của SQLite giúp đọc đồng thời khi FastAPI ghi dữ liệu thời gian thực. |
| **RBAC** | Phân quyền theo vai trò trên dashboard & API: `viewer` (chỉ xem/xuất CSV), `operator` (xem + gửi lệnh + ack cảnh báo), `admin` (toàn quyền + cấu hình + hiệu chuẩn + quản lý user). |
| **`baseline`** | Bộ tham số hiệu chuẩn đã vượt qua nghiệm thu C7 ($\ge 98\%$ trên 100 sản phẩm), được lưu cố định trên flash và bảng `calibrations` để khôi phục nhanh. |
