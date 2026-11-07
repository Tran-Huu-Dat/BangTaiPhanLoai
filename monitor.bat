@echo off
title Pico Serial Monitor - COM5 (115200)
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$port = [System.IO.Ports.SerialPort]::new('COM5', 115200); $port.DtrEnable = $true; $port.RtsEnable = $true; try { $port.Open(); Write-Host '--- Dang ket noi COM5 (Pico 2 W) --- Bấm Ctrl+C de thoat' -ForegroundColor Green; while($true){ if($port.BytesToRead -gt 0){ Write-Host -NoNewline $port.ReadExisting() } Start-Sleep -Milliseconds 20 } } catch { Write-Host 'Loi: Khong mo duoc COM5. Kiem tra xem co chuong trinh khac dang chiem COM5 khong.' -ForegroundColor Red } finally { $port.Dispose() }"

