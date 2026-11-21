@echo off
rem ============================================================
rem  OV-Watch Gateway - default entry point
rem
rem  All real work is in start_gateway.ps1 (PowerShell handles the
rem  non-ASCII user profile path correctly).
rem
rem  Default mode is AUTO: find the USB serial port first, and if it
rem  does not answer the OV+PING probe, fall back to Bluetooth.
rem  So double-clicking this file just works whether the watch is
rem  plugged in by cable or connected over HC-05.
rem
rem  Override with switches after the file name:
rem     start_gateway.cmd -Bt             force Bluetooth
rem     start_gateway.cmd -Port COM7 -Bt  pick a specific port
rem     start_gateway.cmd -Dry            dry run (no real actions)
rem ============================================================
title OV-Watch Gateway
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_gateway.ps1" -Auto %*
echo.
pause
