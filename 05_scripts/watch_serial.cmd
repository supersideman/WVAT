@echo off
rem ============================================================
rem  WVAT - serial monitor (shows [mic] self-test output)
rem  Double-click this file. Press Ctrl+C to quit.
rem
rem  Order matters: wait for "Open COMx @ 115200", THEN press the
rem  RESET button on the board - the mic self-test only runs for
rem  1 second after boot.
rem ============================================================
title WVAT - serial monitor
cd /d "%~dp0.."

set PY=%USERPROFILE%\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe
if not exist "%PY%" goto nopy

echo ================================================
echo   WVAT serial monitor
echo ================================================
echo.
echo   1. Wait until you see:  Open COMx @ 115200
echo   2. THEN press the RESET button on the board
echo   3. Speak during the 1-second self-test
echo   4. Ctrl+C to quit
echo.
echo ================================================
echo.

"%PY%" "05_scripts\serial_monitor.py"

echo.
echo [monitor exited]
echo.
echo   If it said "no CH340 port found":
echo     - is the board's USB cable plugged in?
echo     - is the gateway already running and holding the port?
echo       (close start_gateway.cmd first)
echo.
pause
exit /b 0

:nopy
echo [ERROR] bundled Python 3.12 not found:
echo   %PY%
echo.
echo Ask the assistant - do NOT switch to the system python.
echo.
pause
exit /b 1
