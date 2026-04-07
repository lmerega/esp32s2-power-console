@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ARGS="

:collect
if "%~1"=="" goto run
if /I "%~1"=="bump" (
  set "ARGS=!ARGS! -BumpVersion"
) else (
  set "ARGS=!ARGS! %~1"
)
shift
goto collect

:run
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_bin.ps1" !ARGS!
exit /b %errorlevel%
