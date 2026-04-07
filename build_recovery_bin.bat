@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_recovery_bin.ps1" %*
exit /b %errorlevel%
