@echo off
title Pico Serial Monitor - COM6 (115200)
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$portName = if ('%1' -ne '') { '%1' } elseif ([System.IO.Ports.SerialPort]::GetPortNames() -contains 'COM6') { 'COM6' } else { ([System.IO.Ports.SerialPort]::GetPortNames() | Select-Object -Last 1) }; $port = [System.IO.Ports.SerialPort]::new($portName, 115200); $port.DtrEnable = $true; $port.RtsEnable = $true; try { $port.Open(); Write-Host ('--- Dang ket noi ' + $portName + ' (Pico 2 W) --- Bam Ctrl+C de thoat') -ForegroundColor Green; while($true){ if($port.BytesToRead -gt 0){ Write-Host -NoNewline $port.ReadExisting() } Start-Sleep -Milliseconds 20 } } catch { Write-Host ('Loi: Khong mo duoc ' + $portName + '. Kiem tra xem co chuong trinh khac dang chiem cong nay khong.') -ForegroundColor Red } finally { $port.Dispose() }"


