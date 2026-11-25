# Bộ đặc tả triển khai — Hệ thống phân loại sản phẩm Pico 2 W + IoT Dashboard

Nguồn sự thật về yêu cầu triển khai hệ thống theo bản phân tích thiết kế [`phan-tich-thiet-ke-he-thong.md`](../phan-tich-thiet-ke-he-thong.md). Quyết định hiện trạng đã xác nhận xem ở [`decisions.md`](../decisions.md), sổ tay lỗi ở [`conflicts-and-bugs.md`](../conflicts-and-bugs.md), thuật ngữ ở [`glossary.md`](../glossary.md), tiến độ đợt làm việc ở [`progress.md`](../progress.md).

## 1. Quy ước

- ID spec: `SPEC-<KHU-VỰC>`; yêu cầu: `<KHU-VỰC>-R<nn>`; nghiệm thu: `<KHU-VỰC>-AC<nn>`. Không đánh số lại; bỏ thì ghi `deprecated`.
- Mức ưu tiên: **MUST** (Bắt buộc — phạm vi tối thiểu ngày thi), **SHOULD** (Nên có — lệch phải ghi lý do), **MAY** (Có thể).
- AC theo mẫu *Cho / Khi / Thì*; mỗi AC có `Lệnh kiểm` hoặc quy trình kiểm chứng bench phần cứng rõ ràng:
  - `[]` chưa đạt
  - `[x]` test xanh và có bằng chứng lệnh/kết quả thực tế
  - `[~]` chưa kiểm chứng (ghi rõ lý do và cách đóng; chặn chuyển spec sang `Done`)
- Trạng thái spec: `Draft` → `Ready` (người dùng duyệt yêu cầu, hết câu hỏi chặn) → `In progress` (có đợt thực thi được giao trong `../progress.md`) → `Done` (mọi AC `MUST` của spec đạt `[x]`). Hiện trạng code đã có trước đặc tả: `As-built`.
- Quy tắc **một sự thật, một nơi**: mọi thông số phần cứng, schema, topic MQTT và công thức chỉ định nghĩa chuẩn tại spec sở hữu; các spec khác trỏ bằng link và ID.

## 2. Quyền của AI theo trạng thái spec

| Trạng thái | AI được làm | AI không được làm |
| --- | --- | --- |
| `Draft` | Đọc, nêu chỗ thiếu, đối chiếu hiện trạng, đề xuất hành vi và AC | **Viết code triển khai**; tự coi đề xuất chưa chốt là yêu cầu đã duyệt |
| `Ready` | Lập plan chia lát cắt dọc, mô tả test dự kiến | **Tự bắt đầu code hoặc sửa firmware/backend** chỉ vì spec `Ready` |
| `In progress` | Code + test **trong đợt được giao** ở `../progress.md`, đánh `[x]` AC có bằng chứng | Tự lấy spec/task tiếp theo ngoài đợt; thêm chức năng ngoài phạm vi |
| `Done` | Phân tích lỗi; sửa đúng hành vi đã chốt khi có đợt sửa lỗi | Đổi hành vi hoặc hợp đồng khi chưa cập nhật spec |
| `As-built` | Ghi bằng chứng đối chiếu, nêu khác biệt vào câu hỏi mở | Coi code thử nghiệm hiện có là chuẩn nếu mâu thuẫn với spec được duyệt |

## 3. Danh mục & Bản đồ Năng lực (Capability Map)

Chiều phụ thuộc chỉ đi một chiều (không có vòng lặp).

| ID | File | Nội dung | Phụ thuộc (`Depends on`) | Giai đoạn | Trạng thái |
| --- | --- | --- | --- | --- | --- |
| `SPEC-ARCH` | [`00-architecture.md`](00-architecture.md) | Kiến trúc edge–server 4 lớp, phân lõi RP2350, bản đồ chân, điện/EMC, an toàn phần cứng E-STOP & Watchdog, Docker Compose | — | 1 | Draft |
| `SPEC-SORT` | [`01-firmware-sorting-control.md`](01-firmware-sorting-control.md) | Máy trạng thái 7 trạng thái Core 0, đọc ToF + lọc trung vị, tính chiều cao & phân loại S/M/L, hẹn giờ đẩy $t_d$, giới hạn nhiệt, nút cứng & đèn tháp | `SPEC-ARCH` | 1 | Draft |
| `SPEC-MQTT` | [`02-mqtt-edge-sync.md`](02-mqtt-edge-sync.md) | Core 1 Wi-Fi/MQTT/NTP, bộ đệm vòng $\ge 500$ sự kiện, hợp đồng 10 topic MQTT, xác thực kép & hoãn ghi flash khi `RUNNING` | `SPEC-ARCH`, `SPEC-SORT` | 1 | Draft |
| `SPEC-BACK` | [`03-backend-api-storage.md`](03-backend-api-storage.md) | Mosquitto ACL, FastAPI + `aiomqtt`, JWT + RBAC, lược đồ 9 bảng SQLite/PostgreSQL, REST + WebSocket `/ws/live`, lưu giữ dữ liệu | `SPEC-MQTT` | 1 | Draft |
| `SPEC-DASH` | [`04-web-dashboard-ui.md`](04-web-dashboard-ui.md) | Web SPA giám sát thời gian thực, phân bố chiều cao, cấu hình & đối khớp phiên bản, điều khiển từ xa, cảnh báo, báo cáo CSV, panel sức khỏe & giả lập | `SPEC-BACK` | 1 | Draft |
| `SPEC-CALIB` | [`05-calibration-and-runtime-health.md`](05-calibration-and-runtime-health.md) | Checklist S0–S3, Wizard hiệu chuẩn khóa tuần tự C1–C7 + `baseline`, hợp nhất tốc độ không encoder, giám sát 11 chỉ số khi `RUNNING` | `SPEC-SORT`, `SPEC-MQTT`, `SPEC-BACK`, `SPEC-DASH` | 2 (C1 thuộc GĐ 1) | Draft |

**Thứ tự xây dựng (Build Order):** `SPEC-ARCH` → `SPEC-SORT` → `SPEC-MQTT` → `SPEC-BACK` → `SPEC-DASH` → `SPEC-CALIB`.

## 4. Đối chiếu tài liệu nguồn (Traceability Matrix)

Bảng dưới đây lập chỉ mục các mục trong [`phan-tich-thiet-ke-he-thong.md`](../phan-tich-thiet-ke-he-thong.md) (`FR-01`..`FR-27`, `NFR-01`..`NFR-17`, `UC-01`..`UC-10`, `TC-01`..`TC-09`, `S0`..`S3`, `C1`..`C7`). Có tham chiếu không đồng nghĩa đã đủ tiêu chí nghiệm thu hoặc đã kiểm thử đạt; các khoảng trống được ghi bên dưới và trong biên bản verify.

### 4.1 Yêu cầu chức năng (`FR-01`..`FR-27`)

| Nguồn (`phan-tich-thiet-ke-he-thong.md`) | Spec sở hữu | REQ liên quan |
| --- | --- | --- |
| `FR-01` Phát hiện sản phẩm vào vùng đo từng trạm | `SPEC-SORT` | `SORT-R02` |
| `FR-02` Tính chiều cao $h = H_0 - d$ (sau C2: $((H_0-d)-c)/k$), trung vị $N$ mẫu | `SPEC-SORT`, `SPEC-CALIB` | `SORT-R03`, `CALIB-R03` |
| `FR-03` Phân loại S/M/L theo $T_{SM}, T_{ML}$ cấu hình được | `SPEC-SORT` | `SORT-R04` |
| `FR-04` Trạm 2 đo lại vật qua trạm 1; phát hiện `M` lọt trạm 1 thành lỗi (`D-11`) | `SPEC-SORT` | `SORT-R05` |
| `FR-05` Kích solenoid trạm 1 (`M`) và trạm 2 (`L`) sau $t_d$, xung $t_{on}$ (`D-11`) | `SPEC-SORT` | `SORT-R06`, `SORT-R07` |
| `FR-06` Điều khiển động cơ băng tải: chạy, dừng, PWM ($\le 1\text{ s}$) | `SPEC-SORT` | `SORT-R08` |
| `FR-07` Xác nhận `S` ở cuối băng bằng IR (`SHOULD`, `D-11`) | `SPEC-SORT`, `SPEC-ARCH` | `SORT-R09`, `ARCH-R03` |
| `FR-08` Nút cứng START, STOP, E-STOP và đèn tháp/còi | `SPEC-SORT`, `SPEC-ARCH` | `SORT-R10`, `ARCH-R05` |
| `FR-09` Chế độ AUTO (`RUNNING`), MANUAL, CALIB; chỉ đổi khi dừng | `SPEC-SORT` | `SORT-R01` |
| `FR-10` Hiệu chuẩn $H_0$, lưu flash và hiển thị dashboard | `SPEC-CALIB`, `SPEC-MQTT` | `CALIB-R02`, `MQTT-R09` |
| `FR-11` Phát hiện kẹt, lỗi I²C ToF, mất kết nối → `FAULT` + alarm (`SHOULD`) | `SPEC-SORT` | `SORT-R11` |
| `FR-12` Kết nối Wi-Fi, MQTT, tự kết nối lại | `SPEC-MQTT` | `MQTT-R01`, `MQTT-R02` |
| `FR-13` Gửi `event/item` cho mỗi sản phẩm (`seq`, `ts`, `h1`, `h2`, `cls`, `station`, `result`) | `SPEC-MQTT` | `MQTT-R04` |
| `FR-14` Gửi `telemetry` mỗi 1 s (trạng thái, tốc độ, đếm, thông lượng, RSSI...) | `SPEC-MQTT` | `MQTT-R03` |
| `FR-15` Đệm sự kiện khi mất mạng, gửi bù không trùng | `SPEC-MQTT`, `SPEC-BACK` | `MQTT-R05`, `BACK-R02` |
| `FR-16` Đồng bộ NTP; fallback `uptime_ms` để server tự gán timestamp (`SHOULD`) | `SPEC-MQTT`, `SPEC-BACK` | `MQTT-R06`, `BACK-R03` |
| `FR-17` Dashboard thời gian thực (online/offline, trạng thái, tốc độ, S/M/L, sp/phút) | `SPEC-DASH` | `DASH-R02` |
| `FR-18` Nhật ký sản phẩm + biểu đồ phân bố chiều cao kèm ngưỡng | `SPEC-DASH`, `SPEC-BACK` | `DASH-R03`, `BACK-R05` |
| `FR-19` Danh sách cảnh báo và xác nhận (ack) cảnh báo | `SPEC-DASH`, `SPEC-BACK` | `DASH-R06`, `BACK-R08` |
| `FR-20` Thiết lập $T_{SM}, T_{ML}, t_{d1}/t_{d2}$ (hoặc $L_{p1}/L_{p2}/e_{det}$), $t_{on}$, PWM, $N$; kiểm tra biên 2 lớp | `SPEC-BACK`, `SPEC-MQTT`, `SPEC-DASH` | `BACK-R06`, `MQTT-R07`, `DASH-R04` |
| `FR-21` Pico áp dụng, lưu flash khi `IDLE`, phản hồi `config/reported`; dashboard so khớp | `SPEC-MQTT`, `SPEC-DASH` | `MQTT-R08`, `DASH-R04` |
| `FR-22` Lưu lịch sử phiên bản cấu hình, preset và khôi phục (`SHOULD`) | `SPEC-BACK`, `SPEC-DASH` | `BACK-R06`, `DASH-R04` |
| `FR-23` Điều khiển từ xa (Start, Stop, Reset, Test solenoid, Calibrate) + từ chối khi E-STOP | `SPEC-DASH`, `SPEC-BACK`, `SPEC-MQTT` | `DASH-R05`, `BACK-R07`, `MQTT-R10` |
| `FR-24` Lưu sự kiện, telemetry, cảnh báo, lịch sử lệnh; xuất CSV | `SPEC-BACK`, `SPEC-DASH` | `BACK-R02`, `BACK-R05`, `DASH-R07` |
| `FR-25` Báo cáo phiên chạy tự động khi kết thúc phiên (`SHOULD`) | `SPEC-BACK`, `SPEC-DASH` | `BACK-R09`, `DASH-R07` |
| `FR-26` Đăng nhập JWT, phân quyền Viewer / Operator / Admin | `SPEC-BACK`, `SPEC-DASH` | `BACK-R04`, `DASH-R01` |
| `FR-27` Chế độ giả lập phát dữ liệu (`MAY`) | `SPEC-DASH` | `DASH-R11` |

### 4.2 Yêu cầu phi chức năng (`NFR-01`..`NFR-17`), Use Case (`UC-01`..`UC-10`), Kiểm thử (`TC-01`..`TC-09`), Setup/Hiệu chuẩn (`S0`..`S3`, `C1`..`C7`)

| Nguồn | Spec sở hữu | Mã REQ / AC / Mục tương ứng |
| --- | --- | --- |
| `NFR-01` Độ chính xác phân loại $\ge 98\%$ ($\ge 100$ sp) | `SPEC-SORT`, `SPEC-CALIB` | `SORT-R04`, `SORT-AC04`, `CALIB-R06`, `CALIB-AC07` |
| `NFR-02` Sai số đo $\le \pm 3\text{ mm}$; khoảng cách cỡ $\ge 3\sigma$ | `SPEC-SORT`, `SPEC-CALIB` | `SORT-R03`, `SORT-AC03`, `CALIB-R02`, `CALIB-R03`, `CALIB-R05` |
| `NFR-03` Độ lệch kích solenoid $\le \pm 5\text{ ms}$ độc lập mạng | `SPEC-ARCH`, `SPEC-SORT` | `ARCH-R02`, `SORT-R06`, `SORT-AC05` |
| `NFR-04` Thông lượng mục tiêu $\ge 30\text{ sp/phút}$ (mô hình §9) | `SPEC-SORT` | `SORT-R13`, `SORT-AC09` |
| `NFR-05` Độ trễ sự kiện → dashboard $\le 1\text{ s}$ trong LAN | `SPEC-BACK`, `SPEC-DASH` | `BACK-R10`, `DASH-R02`, `BACK-AC08` |
| `NFR-06` Tự chủ biên khi mất mạng, đệm $\ge 500$ sự kiện | `SPEC-ARCH`, `SPEC-MQTT` | `ARCH-R02`, `MQTT-R05`, `MQTT-AC04` |
| `NFR-07` Tự phục hồi kết nối $\le 10\text{ s}$ | `SPEC-MQTT` | `MQTT-R02`, `MQTT-AC02` |
| `NFR-08` E-STOP phần cứng ngắt động lực $\le 100\text{ ms}$ độc lập MCU | `SPEC-ARCH`, `SPEC-SORT` | `ARCH-R05`, `SORT-R10`, `ARCH-AC03` |
| `NFR-09` Firmware chặn $t_{on}$ tối đa + giới hạn nhiệt $D_{rated}$ | `SPEC-SORT`, `SPEC-CALIB` | `SORT-R07`, `CALIB-R10`, `SORT-AC06` |
| `NFR-10` Watchdog RP2350 reset $\le 1\text{ s}$ về trạng thái an toàn | `SPEC-ARCH`, `SPEC-SORT` | `ARCH-R06`, `ARCH-AC04` |
| `NFR-11` Bảo mật: MQTT user/pass + ACL, dashboard JWT, audit log | `SPEC-MQTT`, `SPEC-BACK` | `MQTT-R01`, `BACK-R01`, `BACK-R04` |
| `NFR-12` Khả dụng server $\ge 99\%$, khởi động 1 lệnh Docker Compose | `SPEC-ARCH`, `SPEC-BACK` | `ARCH-R09`, `BACK-R01`, `ARCH-AC06` |
| `NFR-13` Start/Stop $\le 1$ thao tác, giao diện responsive tablet | `SPEC-DASH` | `DASH-R05`, `DASH-R10`, `DASH-AC07` |
| `NFR-14` Mã module hóa, tham số tách khỏi code, nạp UF2 qua USB | `SPEC-ARCH`, `SPEC-MQTT` | `ARCH-R08`, `MQTT-R08` |
| `NFR-15` Đa thiết bị qua `device_id` trong topic và mọi bảng DB | `SPEC-ARCH`, `SPEC-BACK` | `ARCH-R07`, `BACK-R02`, `BACK-AC07` |
| `NFR-16` Tách nguồn động lực/logic, chung GND, flyback, I²C pull-up | `SPEC-ARCH` | `ARCH-R04`, `ARCH-AC02` |
| `NFR-17` Lưu sự kiện $\ge 30$ ngày; gộp telemetry theo phút sau 7 ngày | `SPEC-BACK` | `BACK-R11`, `BACK-AC09` |

### 4.3 Truy vết từng UC / TC / bước hiệu chuẩn (rà soát 2026-11-25)

| Nguồn | REQ / AC tương ứng | Giới hạn độ phủ hiện tại |
| --- | --- | --- |
| `UC-01` Đăng nhập | `BACK-R04`, `BACK-AC02`, `DASH-AC01` | Có tiêu chí đăng nhập/phân quyền; chưa chạy |
| `UC-02` Giám sát | `DASH-R02`, `DASH-AC02` | Chưa chạy |
| `UC-03` Chạy/dừng | `SORT-R01`, `DASH-R05`, `DASH-AC04` | Cần thống nhất cách đếm thao tác Start có xác nhận |
| `UC-04` Test cơ cấu | `DASH-R05`, `MQTT-R10`, `SORT-AC06` | Cần AC riêng cho từng cơ cấu trong MANUAL |
| `UC-05` Ack cảnh báo | `BACK-R08`, `BACK-AC06`, `DASH-AC05` | Chưa chạy |
| `UC-06` Cấu hình | `BACK-R06`, `BACK-AC04`, `MQTT-AC05`, `DASH-AC03` | Còn thiếu hợp đồng cấu hình đầy đủ |
| `UC-07` Hiệu chuẩn H0 | `CALIB-R02`, `CALIB-AC02`, `BACK-R12` | Cần thống nhất quyền và trạng thái máy |
| `UC-08` Lịch sử/CSV/báo cáo | `BACK-R05`, `BACK-R09`, `DASH-AC06` | Chưa chạy |
| `UC-09` Quản lý người dùng/quyền | `BACK-R04`, `DASH-R01` | Chưa có hợp đồng API và AC thao tác quản trị người dùng |
| `UC-10` Tự động phân loại | `SORT-R01`, `SORT-R04`, `SORT-R05`, `SORT-AC04` | Theo D-11; cần bench |
| `TC-01` Độ chính xác đo | `SORT-AC03`, `CALIB-AC03` | Phân biệt bài nghiệm thu 30 lần/khối với C2 50 mẫu/khối |
| `TC-02` 100 sản phẩm hỗn hợp | `SORT-AC04`, `CALIB-AC07` | Cần bench |
| `TC-03` Jitter kích đẩy | `SORT-AC05`, `ARCH-AC01` | Cần logic analyzer |
| `TC-04` Offline 60 giây | `MQTT-AC04`, `BACK-AC01` | Cần chốt định danh sự kiện trước khi đếm sản phẩm |
| `TC-05` E-STOP mọi trạng thái | `ARCH-AC03` | AC mới liệt kê IDLE/RUNNING/MANUAL và treo Core 0; cần bao phủ các trạng thái còn lại |
| `TC-06` Cấu hình sai ở hai lớp | `BACK-AC04`, `MQTT-AC05` | Cần thống nhất schema và giới hạn |
| `TC-07` Lệnh trái quyền/ESTOP | `BACK-AC03`, `BACK-AC05`, `MQTT-AC06` | Cần thống nhất reset và hiệu chuẩn |
| `TC-08` Watchdog | `ARCH-AC04` | Cần bench |
| `TC-09` Tải 5 thiết bị | `BACK-AC08` | P95 trong expected chưa tương đương giới hạn <=1 s của yêu cầu |
| `S0` Cơ khí/kích thước | `CALIB-R01`, `CALIB-AC07` | Cần số đo thực và biên bản |
| `S1` Điện/E-STOP/50 xung | `CALIB-R01`, `ARCH-AC02`, `ARCH-AC03` | Cần bench |
| `S2` Wi-Fi/MQTT/NTP | `CALIB-R01`, `CALIB-AC07`, `MQTT-AC01` | Cần phép đo riêng sai số đồng hồ <50 ms |
| `S3` XSHUT/ToF | `CALIB-R01`, `CALIB-AC07` | Chờ chốt sơ đồ chân; cần bench |
| `C1` Nền H0 | `CALIB-R02`, `CALIB-AC02` | Chưa chạy |
| `C2` Tuyến tính ToF | `CALIB-R03`, `CALIB-AC03` | SHOULD của bước mâu thuẫn wizard bắt buộc tuần tự |
| `C3` Mô hình tốc độ | `CALIB-R04`, `CALIB-AC07` | Cần fixture đo và thống nhất mức bắt buộc |
| `C4` Thời gian cơ cấu | `CALIB-R05`, `CALIB-AC07` | Cần bench; SHOULD/MUST chưa nhất quán |
| `C5` Ngưỡng phân loại | `CALIB-R05`, `CALIB-AC07` | Cần bench; SHOULD/MUST chưa nhất quán |
| `C6` Căn vị trí đẩy | `CALIB-R05`, `CALIB-AC07` | Cần bench theo M/L của D-11 |
| `C7` Nghiệm thu/baseline | `CALIB-R06`, `CALIB-AC07` | Chưa chốt vòng đời baseline xuyên firmware/backend |

Các nội dung §10.1, §10.4–10.6 được dẫn tại `CALIB-R01..R11`, `MQTT-R09`, `BACK-R02`, `DASH-R08..R09`; đây là chỉ mục, không phải chứng nhận đạt.

## 5. Phần lệch giữa Thiết kế mục tiêu (`.docx`) và Hiện trạng code (`As-built`)

> [!IMPORTANT]
> Repo hiện tại (`main.cpp`, `dashboard/`, `decisions.md`) là **bản nguyên mẫu hiện có**, chưa triển khai theo kiến trúc mục tiêu của tài liệu `.docx`. Không được sửa code nguyên mẫu để áp đặt thiết kế mới khi chưa có quyết định chốt và đợt thực thi được giao.

| # | Hạng mục | Hiện trạng code đã xác minh | Thiết kế mục tiêu (`.docx`) | Trạng thái / Spec xử lý |
| --- | --- | --- | --- | --- |
| 1 | **Phân công trạm đẩy** | Trạm 1 đẩy **M** (65–75 mm), Trạm 2 đẩy **L** (50–60 mm), **S** chạy thẳng (`decisions.md` `D-01`, `main.cpp:13-15`) | Trạm 1 đẩy **S**, Trạm 2 đẩy **M**, **L** chạy thẳng cuối băng (`§1.1`, `FR-03..05`) | **Đã chốt theo code (`D-11`, 2026-11-25):** toàn bộ `SPEC-SORT/MQTT/DASH/CALIB` dùng Trạm 1=`M`, Trạm 2=`L`, `S` cuối băng |
| 2 | **Cách đo & thời gian trễ** | So sánh khoảng cách $d$ trực tiếp, 2 mẫu liên tiếp, `travel_delay_ms = 0`, `on_time_ms = 500 ms` (`decisions.md` `D-02..D-04`, `main.cpp:46-48,110-111`) | Đo chiều cao $h = ((H_0 - d) - c)/k$, trung vị $N$ mẫu (3–5), hẹn giờ $t_d = (L_p + e_{det} + \ell_p/2)/\hat{v} - t_{act}$, chặn nhiệt $D_{rated}$ (`§9`, `§10`) | `SPEC-SORT` §4 (`SORT-R03..07`), đo thông số thực nghiệm theo §6 bên dưới |
| 3 | **Bản đồ chân & phần cứng** | 2 bus I²C riêng (`GP0/1`, `GP6/7`), L298N `GP10/11`, chưa có nút cứng, chưa có E-STOP (`PEGATRON_ESTOP_ABSENT=1`), chưa có PWM động cơ/đèn tháp/IR/ADC (`decisions.md` `D-05`, `D-07`, `D-09`, `main.cpp:32-41`) | Chung bus I²C0 `GP4/5` + XSHUT `GP6/7` (`0x30`/`0x31`), IR `GP8`, Đèn/còi `GP10–13`, Solenoid MOSFET `GP14/15`, PWM/DIR `GP16/17`, START/STOP `GP18/19`, E-STOP opto `GP20`, ADC $V_s$ `GP27`, tùy chọn INA219 `0x40` (`§6.2`, `§10.1`) | `SPEC-ARCH` §9 (`HW_PROFILE_LEGACY` vs `HW_PROFILE_TARGET`) & §12 (`Q-ARCH-01`) |
| 4 | **Hợp đồng MQTT, Backend & DB** | Topic `pegatron/v1/{device_id}/` (`availability`, `telemetry`, `events`, `command`, `ack`, `wifi/*`), 4 bảng SQLite (`samples`, `events`, `commands`, `runs`), chưa JWT/RBAC, chưa `config/*`, `CALIBRATE_TOF` trả `NOT_SUPPORTED` (`dashboard/protocol.py:20-23`, `dashboard/storage.py:12-35`, `docs/dashboard.md:40`) | Topic `vmu/unknow/sorter/{device_id}/` (10 topic gồm `status`, `telemetry`, `event/item`, `alarm`, `config/desired|reported`, `cmd`, `cmd/ack`, `calib/progress|result`), 9 bảng DB, JWT + RBAC (`viewer`/`operator`/`admin`), Docker Compose 3 dịch vụ (`§6.3`, `§7`, `§10.4`) | `SPEC-MQTT` §12 (`Q-MQTT-01`), `SPEC-BACK` §12 (`Q-BACK-01`) |
| 5 | **Cập nhật nội bộ `.docx` (§7.2 → §10.5)** | — | §7.2 minh họa `config/desired` dùng `d1_ms`, `d2_ms`; đến §10.5 chuyển sang tham số hình học `l_p1_mm`, `l_p2_mm`, `e_det_mm` để firmware tự tính $t_d$ từ $\hat{v}$ | `SPEC-MQTT` §4 (`MQTT-R07`), `SPEC-CALIB` §9 |
| 6 | **IR cuối băng (`FR-07` vs `§10`)** | Chưa có cảm biến IR cuối băng (`dashboard/firmware/README.md:5`) | §2 `FR-07` ghi `Nên có` IR cuối băng xác nhận sản phẩm đi thẳng; §10 nêu cấu hình hiện tại không có IR ở máng/cuối băng và đếm S/M/L từ quyết định phân loại | `SPEC-SORT` §4 (`SORT-R09` xếp GĐ2 / `SHOULD`) |

## 6. Định hướng chia module code & quy trình đo thông số trên phần cứng

Để có thể code độc lập từng khối và tự đo toàn bộ thông số phần cứng còn mở (`Q-ARCH-01..03`, `Q-SORT-02..03`, `Q-CALIB-01..02`), tách dự án thành **6 module ranh giới rõ** và chạy kiểm nghiệm tuần tự 4 chặng:

### 6.1 Sáu module đề xuất

| Module | Trách nhiệm cốt lõi | Phụ thuộc phần cứng | Cách kiểm chứng nhanh |
| --- | --- | --- | --- |
| `hw_hal` | Bọc chân GPIO, I²C (`HW_PROFILE_LEGACY` theo `main.cpp` hoặc `HW_PROFILE_TARGET` theo `§6.2`), driver VL53L0X/VL53L1X + `XSHUT`, PWM động cơ, opto E-STOP, ADC `GP27`, tùy chọn INA219 | Phụ thuộc 100% bo mạch thật | Bench đơn lẻ: quét I²C, đọc ToF, bắn xung `MANUAL` |
| `sorter_core` | Thuần C++ (không gọi Pico SDK trực tiếp): FSM 7 trạng thái, lọc trung vị $N$ mẫu, công thức $h$, phân loại `S/M/L` (`D-11`: Trạm 1 = `M`, Trạm 2 = `L`), tính $t_d$, chặn nhiệt $D_{rated}$ | **0 phụ thuộc phần cứng** | Host unit test (`ctest`: `SORT-AC01`, `SORT-AC02`, `SORT-AC06`) |
| `calib_math` | Tính $H_0, \sigma_0$ (C1), hồi quy $(k, c)$ (C2), lọc hợp nhất tốc độ $\hat{v}$ 4 nguồn + loại ngoại lai $\pm 20\%$, 11 chỉ số sức khỏe `§10.5` | **0 phụ thuộc phần cứng** | Host unit test (`ctest`: `CALIB-AC03`, `CALIB-AC06`) |
| `net_mqtt_ringbuf` | Core 1 Wi-Fi/MQTT/NTP, ring buffer $\ge 500$ `event/item`, validator `config/desired`, hoãn ghi flash về `IDLE` | Chạy tách được phần ring buffer/JSON trên host | Host test (`MQTT-AC03`) + bench Mosquitto (`MQTT-AC01..07`) |
| `backend_api_sqlite` | FastAPI + `aiomqtt` + 9 bảng SQLite WAL + JWT/RBAC + REST + `WS /ws/live` + CSV/report | Chạy hoàn toàn trên laptop | `pytest` (`BACK-AC01..09`) |
| `dashboard_spa` | Giao diện web giám sát, cấu hình `desired`/`reported`, điều khiển, cảnh báo, panel sức khỏe & Wizard | Chạy qua trình duyệt với backend | Playwright E2E (`DASH-AC01..07`) |

### 6.2 Lộ trình đo thực nghiệm thông số phần cứng (thay thế giả định trên giấy)

1. **Chặng 1 — Đo cơ–điện tĩnh (`S0`, `S1`, `Q-SORT-03`):**
   - Dùng thước đo khung băng lấy $L_{p1}, L_{p2}, L_{12}, b, B$ (mm) và chiều dài băng $L_{băng}$.
   - Gắn LED song song cuộn solenoid, quay slow-motion 240 fps (`~4 ms/khung`) bắn 10 lần để chốt $t_{act}, t_{push}, t_{ret}$ thật; đối chiếu datasheet solenoid lấy $D_{rated}$ và chọn $t_{on,max}$.
   - Kiểm tra E-STOP NC ngắt contactor $\le 100\text{ ms}$ (`TC-05`) và bắn solenoid 50 lần không reset MCU (`S1`).
2. **Chặng 2 — Đo cảm biến ToF & phôi thật (`Q-SORT-02`, `C1`, `C2`, `C5`):**
   - Chạy băng trống 1 vòng lấy $\ge 200$ mẫu mỗi ToF → ghi nhận $H_0$ và nhiễu nền $\sigma_0$ thật (yêu cầu $\sigma_0 \le 1\text{ mm}$).
   - Đặt 3 khối chuẩn dưới mỗi ToF (50 mẫu/khối) → kiểm tra sai số đo `TC-01` ($\le \pm 3\text{ mm}$) và tính $(k, c)$.
   - Cho 20 phôi thật mỗi cỡ `S/M/L` chạy qua → lấy phân bố chiều cao để chốt $T_{SM}, T_{ML}$ (bảo đảm khoảng cách tới ngưỡng $\ge 3\sigma_{med}$).
3. **Chặng 3 — Đo tốc độ, jitter & tinh chỉnh vị trí đẩy (`C3`, `C6`, `TC-03`):**
   - Chạy ở 4–5 mức PWM đo tốc độ thực $v_{ref}$ và điện áp $V_s$ → khớp $a, b, V_{nom}$ và $D_{eff,S/M/L}$.
   - Chạy 10 sản phẩm mỗi cỡ cần đẩy (`M` ở Trạm 1, `L` ở Trạm 2 theo `D-11`) để tinh chỉnh $e_{det}$ cho bàn đẩy chạm đúng tâm ($\le \pm 5\text{ mm}$).
   - Dùng Logic Analyzer đo độ lệch thời điểm kích solenoid có/không tải Wi-Fi (`TC-03` $\le \pm 5\text{ ms}$).
4. **Chặng 4 — Tích hợp & diễn tập toàn tuyến (`C7`, `TC-02`, `TC-04`, `TC-08`, `TC-09`):**
   - Chạy 100 sản phẩm trộn ngẫu nhiên `S/M/L` kiểm tra tỉ lệ đúng máng $\ge 98\%$ (`TC-02`) → lưu `baseline`.
   - Diễn tập mất router 60 s (`TC-04`), ép treo Watchdog (`TC-08`), và kiểm tải server 5 thiết bị (`TC-09`).
