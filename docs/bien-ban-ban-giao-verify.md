# Biên bản bàn giao & Hướng dẫn Verify độc lập

## Kết quả đối chiếu lại 2026-11-25

- Phạm vi bàn giao ban đầu: **19 file = 7 PNG + 12 MD**, trong đó 12 MD bao gồm chính biên bản này (11 MD còn lại được liệt kê bên dưới).
- Đối chiếu DOCX: đủ 10 mục, 24 bảng không lệch nội dung ô sau chuẩn hóa, 74 mã nguồn, 23 đoạn có công thức; 7 PNG khớp SHA-256 với media gốc và đúng thứ tự chú thích.
- Kiểm tra 196 đích liên kết tương đối: không thiếu file; chưa xác nhận anchor/render. 142 tham chiếu R/AC sau mở rộng đều có định nghĩa; ánh xạ nhóm UC/TC/S/C chưa chứng minh đủ ý nghĩa từng mục.
- Phát hiện và sửa: ví dụ trung vị SPEC-SORT §9 sai; 5 khối JSON trong SPEC-MQTT/CALIB chứa comment hoặc nhiều bản tin. Sau sửa, 7 khối JSON spec parse thành công. Bản Markdown chuyển đổi nguồn có 7 khối JSON parse hợp lệ.
- D-11 nối sau D-10 và SORT-R05 khớp phân công trạm. Không có baseline Git để chứng minh D-01..10 chưa từng đổi.
- Đợt code mới theo yêu cầu người dùng: thêm `sorter_core.h` và `tests/test_sorter_core.cpp`, chỉ phân loại/định tuyến; chưa tích hợp vào firmware. Compile/link đạt; chạy EXE bị Windows Application Control chặn, không coi là test pass. 5 test Python cũ đạt.
- Toàn bộ spec vẫn Draft, chưa nghiệm thu phần cứng. Phần 3 và 4 bên dưới là phạm vi/kết quả tự báo cáo của đợt bàn giao ban đầu, không phải chứng nhận toàn bộ spec đã đạt.
- Đã rà soát tiếp hợp đồng và độ phủ của 6 spec; kết luận: **chưa đủ điều kiện đóng băng spec để tích hợp toàn hệ thống**. Các phát hiện bên dưới là mâu thuẫn/thiếu đặc tả, không phải lỗi đã tái hiện trên firmware. Chưa ký đạt toàn bộ checklist 6 bước.

### Phát hiện hợp đồng và độ phủ — 2026-11-25

`P1`: cần giải quyết trước khi triển khai phần phụ thuộc; `P2`: cần bổ sung để nghiệm thu nhất quán. Dòng dẫn là vị trí tại thời điểm rà soát. Không tự nâng các đề xuất dưới đây thành quyết định D mới.

| ID | Mức | Bằng chứng | Kết luận / việc cần chốt |
| --- | --- | --- | --- |
| V-01 | P1 | SPEC-MQTT:140, SPEC-BACK:109 | `seq` tăng đơn điệu và UNIQUE(device_id,seq) chưa có vòng đời qua reboot/wrap/reset_counter. Có nguy cơ bỏ sản phẩm mới như bản trùng; tách bộ đếm hiển thị khỏi định danh sự kiện và định nghĩa cấp ID bền vững. |
| V-02 | P1 | SPEC-MQTT:146, SPEC-BACK:110 | Cache cmd_id gần đây + non-retain chưa đủ bảo đảm không chạy lại sau reboot; thiếu thời hạn lệnh, retry cùng ID, ACK trùng/muộn và đối soát timeout. |
| V-03 | P1 | SPEC-DASH:88, SPEC-BACK:87, SPEC-CALIB:123 | Điều khiển Operator gồm hiệu chuẩn nhưng CALIB yêu cầu Admin. Cần ma trận quyền theo loại lệnh, kiểm tra ở backend và AC từ chối Operator. |
| V-04 | P1 | SPEC-CALIB:13,153; SPEC-MQTT:105; SPEC-BACK:68 | Baseline đầy đủ k/c, mô hình tốc độ, timing và ngưỡng nhưng config/desired chỉ minh họa cấu hình cơ bản. Thiếu schema đầy đủ theo trạm, restore và quan hệ ver/calib_ver. |
| V-05 | P1 | SPEC-CALIB:75,111; SPEC-MQTT:87 | Result retain thiếu định nghĩa lượt hiệu chuẩn/correlation; bản ok cũ có thể mở khóa lượt mới. Cần ID lượt, version allocation, resume và hủy kết quả bước sau khi đo lại. |
| V-06 | P1 | SPEC-CALIB:111,116; SPEC-DASH:92 | Khóa tuần tự toàn C1–C7 mâu thuẫn hướng dẫn hiệu chuẩn lại theo tập con. Cần quy tắc kế thừa kết quả, vô hiệu hóa và khi nào bắt buộc C7. |
| V-07 | P1 | SPEC-MQTT:140, ví dụ :89; SPEC-BACK:150 | D-11 đúng nhưng chưa định nghĩa sự kiện kết thúc S, M lọt, đẩy lỗi, sản phẩm bị dừng giữa đường và liên kết hai trạm. `pushed` phải phân biệt đã ra lệnh với xác nhận cơ khí; quy tắc đếm không được tự suy diễn. |
| V-08 | P1 | SPEC-MQTT:92; SPEC-BACK:83,89 | Item thiếu run identity; thời gian nhận thay ts=null không xác định đúng phiên khi replay sau STOP/START. Cần liên kết phiên tại biên, phân biệt thời điểm xảy ra/nhận và START/STOP tại nút cứng. |
| V-09 | P1 | SPEC-MQTT:41,141; SPEC-BACK:101,150 | Đệm khi mất MQTT chưa bảo đảm lưu DB khi backend tắt nhưng broker còn sống. Thiếu thời điểm giải phóng buffer, độ bền broker/subscription và transaction insert+counter. Nêu rõ giới hạn mất nguồn của RAM thay vì hứa không mất dữ liệu vô điều kiện. |
| V-10 | P2 | SPEC-MQTT:144,165; SPEC-DASH:112 | reported mới sau áp dụng RAM nhưng UI diễn giải lệch ver là chờ flash. Cần tách applied/persisted và đồng bộ trạng thái thật sau reboot. |
| V-11 | P2 | SPEC-DASH:85,91; SPEC-MQTT:139; SPEC-BACK:90 | Thiếu producer/schema cho nguồn điều khiển, chiều dài suy ra/dòng tùy chọn, tham chiếu baseline; WS thiếu envelope type/device và snapshot reconnect. Cần trạng thái unavailable/stale. |
| V-12 | P1 | SPEC-MQTT:195; SPEC-DASH:87; SPEC-BACK:167 | Hình học hoặc delay fallback chưa có ưu tiên, partial/full update và giới hạn chung. Cần chốt N có gồm 4 không, kiểu số, max xung và miền e_det trước validator/timer. |
| V-13 | P2 | README §4; BACK-R04; DASH-R01 | Đã sửa NFR-01/NFR-02 trỏ nhầm và tách đủ 30 mục UC/TC/S/C. UC-09 chỉ nêu quyền, chưa đủ API/AC quản trị người dùng; TC-05 chưa liệt kê mọi trạng thái; S2 thiếu phép đo NTP riêng; UC-04 thiếu AC từng cơ cấu. |
| V-14 | P2 | SPEC-DASH:14,88; BACK-AC08 | Start <=1 thao tác nhưng có xác nhận: cần định nghĩa cách tính. BACK-AC08 dùng P95 <=1s trong khi yêu cầu là độ trễ <=1s; chưa được tự thay giới hạn nguồn bằng percentile. |
| V-15 | P2 | CALIB-R03..R06; SPEC-CALIB:6; DASH-R09 | SHOULD cho C2–C6 nhưng wizard bắt buộc toàn chuỗi; dashboard giai đoạn 1 yêu cầu wizard của giai đoạn 2. Cần thống nhất mức bắt buộc và phạm vi từng đợt. |

Làm rõ thêm: token WS trong query phải được loại khỏi log và có vòng đời; lịch sử calib/progress QoS 0 chỉ best-effort nếu không nâng hợp đồng truyền. Đây là thiếu đặc tả, chưa phải bằng chứng rò token hay mất gói thực tế.

Chi tiết truy vết từng mục nằm tại [README §4.3](specs/README.md#43-truy-vết-từng-uc--tc--bước-hiệu-chuẩn-rà-soát-2026-11-25). Bản chuyển đổi DOCX được giữ nguyên; chỉ sửa lỗi truy vết hiển nhiên, không tự quyết chính sách vận hành.

- **Ngày lập:** 2026-11-25
- **Dự án:** `BangTaiPhanLoai` (`D:\Workspace\Pegatron\program\BangTaiPhanLoai`)
- **Mục tiêu đợt việc:** Chuyển đổi tài liệu `.docx` sang Markdown, xây dựng bộ đặc tả triển khai theo `writing-spec-for-ai`, cập nhật quyết định phân công trạm đẩy (`D-11`), và bổ sung lộ trình chia module + đo kiểm phần cứng.

---

## 1. Danh sách sản phẩm đã tạo & cập nhật

### 1.1 Tài liệu chuyển đổi nguyên bản (giữ nguyên trung thực với `.docx`)
| File | Vai trò |
| --- | --- |
| [`phan-tich-thiet-ke-he-thong.md`](phan-tich-thiet-ke-he-thong.md) | Bản chuyển đổi nguyên vẹn 10 mục từ `Phân tích & Thiết kế hệ thống phân loại sản phẩm theo chiều cao – Pico 2 + IoT D (1).docx` (bảng `FR`, `NFR`, `PDS`, `UC`, `TC`, LaTeX, JSON, checklist `S0–S3`, `C1–C7`). **Không chỉnh sửa nội dung gốc** khi có ghi đè `D-11`. |
| [`assets/phan-tich-thiet-ke/fig1..fig7.png`](assets/phan-tich-thiet-ke) | 7 sơ đồ PNG trích xuất trực tiếp từ `word/media/image1..7.png` (BPMN vận hành, BPMN vòng phân loại, Kiến trúc 4 lớp, DFD mức 1, FSM Core 0, Timeline sản phẩm, Vòng đời hiệu chuẩn). |

### 1.2 Bộ đặc tả triển khai & Tài liệu nền (`Draft`)
| File | Vai trò |
| --- | --- |
| [`decisions.md`](decisions.md) | Bổ sung quyết định `D-11` (giữ nguyên `D-01`..`D-10`). |
| [`glossary.md`](glossary.md) | Bảng thuật ngữ, ký hiệu vật lý/toán học (`§9–§10`) và thuật ngữ phần cứng/IoT. |
| [`specs/README.md`](specs/README.md) | Chỉ mục bộ spec, Capability Map, Build Order, Ma trận truy vết 100% (`FR-01..27`, `NFR-01..17`, `UC-01..10`, `TC-01..09`, `S0..S3`, `C1..C7`), Bảng xung đột hiện trạng và **§6 Định hướng chia 6 module code + 4 chặng đo bench phần cứng**. |
| [`specs/00-architecture.md`](specs/00-architecture.md) | `SPEC-ARCH`: kiến trúc 4 lớp, phân lõi RP2350 Core 0/1 qua FIFO, bản đồ chân `§6.2` (khuyến nghị lớp HAL `HW_PROFILE_LEGACY` / `HW_PROFILE_TARGET`), EMC, E-STOP NC phần cứng, Watchdog, Docker Compose 3 dịch vụ. |
| [`specs/01-firmware-sorting-control.md`](specs/01-firmware-sorting-control.md) | `SPEC-SORT`: FSM 7 trạng thái Core 0, đọc ToF `XSHUT` (`0x30`/`0x31`), trung vị $N$ mẫu, phân loại theo **`D-11` (Trạm 1 = `M`, Trạm 2 = `L`, `S` cuối băng)**, neo alarm timer theo $t_0$, chặn nhiệt $D_{rated}$, PWM động cơ, nút cứng & đèn tháp. |
| [`specs/02-mqtt-edge-sync.md`](specs/02-mqtt-edge-sync.md) | `SPEC-MQTT`: Core 1 Wi-Fi/MQTT/NTP, ring buffer $\ge 500$ sự kiện chống trùng `(device_id, seq)`, hợp đồng 10 topic `vmu/unknow/sorter/{device_id}/`, kiểm tra biên kép và hoãn ghi flash về `IDLE`. |
| [`specs/03-backend-api-storage.md`](specs/03-backend-api-storage.md) | `SPEC-BACK`: FastAPI + `aiomqtt` + 9 bảng SQLite WAL/PostgreSQL + JWT/RBAC (`viewer`/`operator`/`admin`) + REST + `WS /ws/live` + CSV/report + chính sách giữ/gộp dữ liệu. |
| [`specs/04-web-dashboard-ui.md`](specs/04-web-dashboard-ui.md) | `SPEC-DASH`: Web SPA phục vụ từ backend LAN, giám sát live + histogram, đối khớp `desired` vs `reported`, điều khiển an toàn, ack cảnh báo, báo cáo CSV, panel sức khỏe & Wizard `C1–C7`. |
| [`specs/05-calibration-and-runtime-health.md`](specs/05-calibration-and-runtime-health.md) | `SPEC-CALIB`: Checklist `S0–S3`, Wizard khóa tuần tự `C1–C7` + `baseline`, hợp nhất tốc độ 4 nguồn không encoder (cập nhật tác động `D-11`: `S/L` cho đo $\hat{v}_{12}$, lô toàn `M` dùng $\hat{v}_{occ}$), giám sát 11 chỉ số khi `RUNNING`. |
| [`progress.md`](progress.md) | Cập nhật điểm dừng đợt 2026-11-25 (giữ thông tin đợt 2026-11-23). |

---

## 2. Quyết định đã chốt & Thay đổi quan trọng

1. **Quyết định [`D-11`](decisions.md#L17):** Giữ phân công trạm đẩy của bộ đặc tả đúng theo code hiện trạng ([`main.cpp:13-15`](../main.cpp#L13-L15), `D-01`):
   - **Trạm 1:** Đẩy cỡ **M** ($T_{SM} < h_1 \le T_{ML}$).
   - **Trạm 2:** Đo lại toàn bộ sản phẩm qua Trạm 1; đẩy cỡ **L** ($h_2 > T_{ML}$); cho **S** chạy thẳng về cuối băng ($h_2 \le T_{SM}$); nếu phát hiện **M lọt qua Trạm 1** ($T_{SM} < h_2 \le T_{ML}$) thì **không** đẩy vào máng L mà ghi lỗi `missed` + cảnh báo.
   - Ghi đè phân công trạm trong `.docx §1.1, FR-04, FR-05, FR-07`. File chuyển đổi [`phan-tich-thiet-ke-he-thong.md`](phan-tich-thiet-ke-he-thong.md) được giữ nguyên theo tài liệu Word gốc; sự khác biệt được ghi rõ tại [`specs/README.md §5`](specs/README.md#5-phần-lệch-giữa-thiết-kế-mục-tiêu-docx-và-hiện-trạng-code-as-built).
2. **Thông số phần cứng & thể lệ còn mở (`Q-ARCH-01..03`, `Q-SORT-02..03`, `Q-CALIB-01..02`):** Không bịa số đo; được gom thành lộ trình chia 6 module và 4 chặng đo trực tiếp trên phần cứng tại [`specs/README.md §6`](specs/README.md#6-định-hướng-chia-module-code--quy-trình-đo-thông-số-trên-phần-cứng).

---

## 3. Phạm vi KHÔNG đụng tới (giữ nguyên hiện trạng)

- **Không sửa** `main.cpp`, `main_micropython.py`, `test.cpp`, `CMakeLists.txt`, `dashboard/*`, `README.md`, `docs/conflicts-and-bugs.md`, `docs/dashboard.md`, `docs/debug-cam-bien.md`.
- **Không sửa** file `.docx` gốc.
- **Không build hoặc nạp firmware `.uf2`** vì đợt này chỉ lập đặc tả (`Draft`).

---

## 4. Kết quả tự kiểm đã chạy (trước bàn giao)

- Kiểm tra tự động bằng script (sẽ chạy lại ở bước chốt sau chỉnh sửa `D-11`):
  - Đủ **7/7 file PNG** ảnh sơ đồ trích xuất khác rỗng.
  - Đủ **11/11 file Markdown** thuộc phạm vi bàn giao tồn tại và khác rỗng.
  - Toàn bộ khối JSON mẫu parse hợp lệ; toàn bộ liên kết tương đối hợp lệ.
  - Độ phủ **74/74 mã yêu cầu/kiểm thử** từ nguồn (`FR-01..27`, `NFR-01..17`, `UC-01..10`, `TC-01..09`, `S0..S3`, `C1..C7`).
  - Tính nhất quán `D-11`: toàn bộ `01-firmware-sorting-control.md`, `02-mqtt-edge-sync.md`, `04-web-dashboard-ui.md`, `05-calibration-and-runtime-health.md` đã đồng bộ Trạm 1 = `M`, Trạm 2 = `L`, `S` cuối băng.

---

## 5. Checklist dành cho Người / AI Verify độc lập

Người nhận verify (hoặc mở phiên AI mới với chỉ dẫn chỉ đọc) cần kiểm tra 6 mục sau:

1. **Đối chiếu `.docx` → `docs/phan-tich-thiet-ke-he-thong.md`:**
   - Mở song song file `.docx` gốc và [`phan-tich-thiet-ke-he-thong.md`](phan-tich-thiet-ke-he-thong.md); kiểm tra đủ 10 mục, 7 hình ảnh `fig1..fig7.png` khớp đúng vị trí chú thích, không mất bảng `FR/NFR/PDS/UC/TC/§10.5`, công thức LaTeX và JSON không bị dính/mất dòng.
2. **Verify tính nhất quán của `D-11`:**
   - Kiểm tra [`decisions.md`](decisions.md) có `D-11` nối tiếp `D-10` mà không sửa `D-01..D-10`.
   - Tìm kiếm trong `docs/specs/*.md` đảm bảo **không còn** ví dụ hoặc yêu cầu nói Trạm 1 đẩy `S`, Trạm 2 đẩy `M`, hay `L` đi thẳng (trừ bảng đối chiếu lịch sử ở `specs/README.md §5`).
3. **Verify độ phủ & truy vết (Traceability):**
   - Đối chiếu bảng [`specs/README.md §4`](specs/README.md#4-đối-chiếu-tài-liệu-nguồn-traceability-matrix): mọi `FR-01..27`, `NFR-01..17`, `UC-01..10`, `TC-01..09`, `S0..S3`, `C1..C7` đều phải trỏ tới ít nhất một `SPEC-*` và `REQ-ID` có thật trong file spec đích.
4. **Verify tính trung thực thông số (Anti-hallucination):**
   - Kiểm tra trong các spec xem có con số vật lý nào của khung máy thật ($L_{p1}, L_{12}, t_{act}, t_{ret}, D_{rated}$, kích thước phôi thi) bị ghi thành cam kết cứng khi chưa đo hay không; các con số ví dụ trong `§9.10` / `§7.2` phải chỉ dùng làm minh họa, thông số chưa đo phải nằm ở bảng Câu hỏi mở hoặc chặng đo thực nghiệm.
5. **Verify cấu trúc spec & liên kết:**
   - Kiểm tra mỗi spec có đủ: Bản đọc ngắn, Mermaid, Concrete I/O Snapshot, Yêu cầu EARS kèm nhãn nguồn, Bảng kiểm thử `Cho / Khi / Thì`, Thẻ giải thích kỹ thuật, Câu hỏi mở; không còn placeholder mẫu (`SPEC-XXX`, `XXX-R01`, `<tên dự án>`).
   - Kiểm tra toàn bộ liên kết markdown `[...](...)` và ảnh `![...](...)` đều trỏ tới file tồn tại.
6. **Verify trạng thái Git & hiện trạng code:**
   - Chạy `git status --short` và `git diff --stat` tại `D:\Workspace\Pegatron\program\BangTaiPhanLoai`: xác nhận **0 file code nguồn** (`main.cpp`, `dashboard/*`) bị sửa ngoài ý muốn.
   - Lưu ý: [`.gitignore`](../.gitignore) có dòng `/docs/*` (chỉ ngoại lệ `debug-cam-bien.md` và `conflicts-and-bugs.md`), nên nếu muốn lưu trữ các file mới trên Git cần cập nhật `.gitignore` hoặc dùng `git add -f <file>`.
