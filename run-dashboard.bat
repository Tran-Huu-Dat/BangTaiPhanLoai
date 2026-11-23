@echo off
setlocal

set "DASHBOARD_DIR=%~dp0dashboard"
set "PYTHON=%DASHBOARD_DIR%\.venv\Scripts\python.exe"

if not exist "%PYTHON%" (
    echo Chua tim thay moi truong Python cua dashboard.
    echo Hay chay cac lenh sau mot lan trong PowerShell:
    echo   cd /d "%DASHBOARD_DIR%"
    echo   python -m venv .venv
    echo   .\.venv\Scripts\python.exe -m pip install -r requirements-test.txt
    pause
    exit /b 1
)

"%PYTHON%" -c "import fastapi, uvicorn, paho.mqtt.client, amqtt" >nul 2>&1
if errorlevel 1 (
    echo Thieu goi phu thuoc cho dashboard.
    echo Hay chay lenh sau mot lan trong PowerShell:
    echo   cd /d "%DASHBOARD_DIR%"
    echo   .\.venv\Scripts\python.exe -m pip install -r requirements-test.txt
    pause
    exit /b 1
)

start "PEGATRON MQTT Broker" /D "%DASHBOARD_DIR%" "%PYTHON%" broker\dev_broker.py
start "PEGATRON Dashboard" /D "%DASHBOARD_DIR%" "%PYTHON%" -m uvicorn app:app --host 127.0.0.1 --port 8000
start "PEGATRON Simulator" /D "%DASHBOARD_DIR%" "%PYTHON%" simulator.py --interactive

timeout /t 2 /nobreak >nul
start "" "http://127.0.0.1:8000"

echo Dashboard dang chay. Khong dong ba cua so broker, dashboard va simulator.
echo De dung, nhan Ctrl+C trong tung cua so.
endlocal
