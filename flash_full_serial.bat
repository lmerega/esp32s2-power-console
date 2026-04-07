@echo off
setlocal
if "%~1"=="" (
  echo Uso: flash_full_serial.bat COM3 [baudrate] [argomenti extra]
  echo Esempi:
  echo   flash_full_serial.bat COM3
  echo   flash_full_serial.bat COM3 921600
  echo   flash_full_serial.bat COM3 921600 -PreflightOnly
  exit /b 1
)
set PORT=%~1
set BAUD=%~2
if "%BAUD%"=="" (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash_full_serial.ps1" -Port "%PORT%" %3 %4 %5 %6 %7 %8 %9
) else (
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash_full_serial.ps1" -Port "%PORT%" -BaudRate %BAUD% %3 %4 %5 %6 %7 %8 %9
)
exit /b %errorlevel%
