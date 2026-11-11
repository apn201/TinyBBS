@echo off
setlocal
cd /d "%~dp0.."

rem This uses the PlatformIO installation already present on this PC.
"C:\Users\juha_\.platformio\penv\Scripts\pio.exe" run >> "%~dp0..\.pio\scheduled-build.log" 2>&1
exit /b %errorlevel%
