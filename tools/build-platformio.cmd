@echo off
setlocal
cd /d "%~dp0.."

rem Uses the PlatformIO installation in the current user profile.
"%USERPROFILE%\.platformio\penv\Scripts\pio.exe" run >> "%~dp0..\.pio\scheduled-build.log" 2>&1
exit /b %errorlevel%
