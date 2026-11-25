# Debug cảm biến ToF trên Windows

## Đúng firmware trước khi gõ lệnh

Tại phiên kiểm tra ngày 2026-11-22, board chạy **C++**, xuất log qua **COM6, 115200**, ghi nhận khoảng **620 mm / 95 mm**; chưa xác nhận độ chính xác hay loại chip. Số COM có thể thay đổi khi cắm lại USB. `main.cpp` chỉ in trạng thái, không đọc lệnh `getchar`/`scanf`: gõ `i2c.scan()` hoặc `status` không chạy lệnh. [Vòng lặp](../main.cpp#L379)

Chân theo **GP**, không phải số chân vật lý:

- C++ `main.cpp`: ToF1 I2C0 **SDA GP0, SCL GP1**; ToF2 I2C1 **SDA GP6, SCL GP7**, 100 kHz. [Cấu hình](../main.cpp#L27)
- `main_micropython.py` và `test.cpp` dùng ToF2 **GP2/GP3**. Vì khác dây hiện tại, không nạp `TestToF` nguyên trạng để kết luận cảm biến hỏng. [Python](../main_micropython.py#L88), [TestToF](../test.cpp#L7)

## Đọc log C++: không cần cài thư viện

Dùng **Windows PowerShell 5.1** (`powershell.exe`) có sẵn .NET Framework. Đóng Serial Monitor/Thonny/terminal đang giữ cổng trước khi mở. Liệt kê tên COM; danh sách này không xác định thiết bị nào là Pico:

```powershell
[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
```

Ghi khoảng 30 giây vào thư mục tạm, đồng thời hiện log. Đổi `COM6` nếu cần. Đoạn này không gửi lệnh; `ReadExisting()` và `Write()` giữ nguyên dòng qua các lần đọc. `CreateNew` tránh ghi đè. [API SerialPort](https://learn.microsoft.com/en-us/dotnet/api/system.io.ports.serialport?view=netframework-4.8)

```powershell
$log = Join-Path $env:TEMP ('tof-{0}.log' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
$serial = [System.IO.Ports.SerialPort]::new('COM6', 115200, 'None', 8, 'One')
$serial.Handshake = 'None'
$serial.DtrEnable = $true
$serial.RtsEnable = $false
$writer = $null
try {
    $writer = [System.IO.StreamWriter]::new([System.IO.File]::Open(
        $log, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write))
    $writer.AutoFlush = $true
    $serial.Open()
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt 30) {
        $chunk = $serial.ReadExisting()
        $writer.Write($chunk)
        if ($chunk.Length) { Write-Host -NoNewline $chunk }
        Start-Sleep -Milliseconds 50
    }
    $writer.Write($serial.ReadExisting())
} finally {
    try { $serial.Dispose() }
    finally { if ($null -ne $writer) { $writer.Dispose() } }
}
Write-Host "`nLog: $log"
```

`DtrEnable` bật tín hiệu DTR; Pico SDK mặc định dùng DTR để kiểm tra kết nối CDC. Mở serial không đảm bảo nhận lại log khởi động đã trôi qua. [Microsoft DTR](https://learn.microsoft.com/en-us/dotnet/api/system.io.ports.serialport.dtrenable?view=netframework-4.8), [Pico SDK](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_stdio_usb/include/pico/stdio_usb.h)

Lọc lỗi hoặc khoảng cách từ file vừa thu:

```powershell
Select-String -LiteralPath $log -Pattern 'TOF_DIAG|LOI|NO_ACK|I2C_TIMEOUT|SHORT_TRANSFER|MAT_DU_LIEU'
Select-String -LiteralPath $log -Pattern 'TRAM1:|TRAM2:' | Select-Object -Last 20
```

Access denied: kiểm tra ứng dụng đang giữ COM. Cổng không tồn tại: liệt kê lại. File rỗng: kiểm tra board/cổng, DTR, USB; chưa đủ kết luận lỗi I2C. Không commit log tạm.

## Đọc chẩn đoán hiện có

Log mỗi 250 ms; quá 800 ms thiếu dữ liệu sẽ báo mất tín hiệu, thử init lại theo mốc 2500 ms. `TOF_DIAG` chỉ in khi init thất bại. [Thời gian](../main.cpp#L44), [đọc](../main.cpp#L183), [thử lại](../main.cpp#L232)

- `NO_ACK_OR_BUS_ERROR`, thường `rc=-1`: giao dịch không nhận ACK/không có thiết bị hoặc lỗi bus. Kiểm tra chân đúng phiên bản, nguồn, GND chung, SDA/SCL, trạng thái chân enable/XSHUT theo module; không xác định nguyên nhân duy nhất từ mã này.
- `I2C_TIMEOUT`, `rc=-2`: giao dịch quá hạn. `SHORT_TRANSFER`: số byte trả về không bằng số byte driver yêu cầu, xem thêm `rc`, `op`, `reg`. Pico SDK dùng địa chỉ 7-bit và trả số byte khi thành công. [Tài liệu I2C SDK](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_i2c), [mã lỗi SDK](https://github.com/raspberrypi/pico-sdk/blob/master/src/common/pico_base_headers/include/pico/error.h)
- `op=SELECT_REG/READ/WRITE`, `reg=0x....`: thao tác và thanh ghi của lỗi giao dịch đầu tiên được driver lưu; không phải danh sách mọi lỗi. [Driver](../vl53l1x.cpp#L119)
- `stage=MODEL_ID/BOOT/FIRST_MEASUREMENT/CONFIG`: giai đoạn khởi tạo đã tới. `boot` là byte đọc tại `0x00E5` trong lần init này; giá trị mặc định có thể chưa được đọc nếu hỏng sớm. Stage là nhãn của driver tùy chỉnh, không phải mã lỗi ST. [Init](../vl53l1x.cpp#L203)
- `SDA_IDLE`, `SCL_IDLE`: mẫu mức chân lấy **trước** init. Mức 0 gợi ý đường bị giữ thấp; mức 1 không chứng minh thiết bị có ACK hay tín hiệu đạt chuẩn. I2C khi rảnh thường có cả hai đường cao nhờ pull-up. [Vị trí lấy mẫu](../main.cpp#L159), [ST, mục 4](https://www.st.com/resource/en/datasheet/vl53l1x.pdf)
- `MAT_DU_LIEU`: đã init nhưng sau đó mất dữ liệu; `LOI_KHOI_TAO`: init thất bại khi không có chi tiết lỗi giao dịch. `d= ----mm`: chưa có phép đo hợp lệ, có thể do trạng thái đo hoặc ngoài dải; không đồng nghĩa đứt dây. C++ chỉ nhận `status == 0`, `0 < d < 4000`. [Trạng thái](../main.cpp#L327), [điều kiện đo](../main.cpp#L189)

## Chỉ khi board đã chạy MicroPython và có dấu nhắc `>>>`

Không áp dụng phần này cho C++ hiện tại. Chỉ thao tác REPL khi chương trình điều khiển đã dừng và cơ cấu ở trạng thái an toàn. RP2 cung cấp REPL qua USB serial; `machine.I2C` cho quét và đọc thanh ghi. [RP2 quick reference](https://docs.micropython.org/en/latest/rp2/quickref.html)

Ví dụ theo **dây C++ hiện tại** GP0/1, GP6/7; nếu đang dùng dây của file Python thì đổi bus thứ hai thành SDA GP2, SCL GP3:

```python
from machine import I2C, Pin
i2c0 = I2C(0, sda=Pin(0), scl=Pin(1), freq=100_000)
i2c1 = I2C(1, sda=Pin(6), scl=Pin(7), freq=100_000)
print('I2C0', [hex(a) for a in i2c0.scan()])
print('I2C1', [hex(a) for a in i2c1.scan()])
```

`['0x29']` trên mỗi bus là ACK tại địa chỉ đó, chưa xác nhận loại chip hoặc phép đo. `[]` nghĩa là lần quét không nhận ACK. Hai bus độc lập có thể cùng dùng 0x29. API MicroPython dùng **7-bit 0x29**; ST ghi **byte địa chỉ 0x52 ghi / 0x53 đọc**, tương ứng cùng địa chỉ, không truyền 0x52 vào `scan/readfrom_mem`. [MicroPython I2C](https://docs.micropython.org/en/latest/library/machine.I2C.html), [ST, mục 4](https://www.st.com/resource/en/datasheet/vl53l1x.pdf)

Nếu có căn cứ module là VL53L1X, đọc ba byte ID bằng địa chỉ thanh ghi **16-bit** (đổi `bus` để kiểm tra bus còn lại):

```python
bus = i2c0
print(bus.readfrom_mem(0x29, 0x010F, 3, addrsize=16).hex())
```

ST công bố sau reset mới, chưa nạp driver: `0x010F=EA`, `0x0110=CC`, `0x0111=10`, tức `eacc10`. VL53L0X dùng thanh ghi **8-bit**: `bus.readfrom_mem(0x29, 0xC0, 3).hex()` tương ứng `eeaa10` trong điều kiện đó. Chỉ dùng giao thức phù hợp module, không thử cả hai mù quáng; lỗi đọc không tự chứng minh chip thuộc loại khác. [ST VL53L1X, bảng 8](https://www.st.com/resource/en/datasheet/vl53l1x.pdf), [ST VL53L0X, bảng 5](https://www.st.com/resource/datasheet/vl53l0x.pdf)

Driver C++ hiện tại chỉ chấp nhận byte cao ID là `EB` hoặc `EA`, đồng thời dùng bảng `VL53L4CD_INIT_SEQ`. Vì vậy tên lớp `VL53L1X`, init thành công hay `model=0xEB..` **không đủ chứng nhận chip VL53L1X hoặc driver phù hợp mọi model**. Đối chiếu ký hiệu module, ID thật và driver ST tương ứng trước khi đổi cấu hình đo. [Kiểm tra ID](../vl53l1x.cpp#L205), [bảng cấu hình](../vl53l1x.cpp#L232)
