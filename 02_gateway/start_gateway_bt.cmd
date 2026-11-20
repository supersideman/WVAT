@echo off
rem ============================================================
rem  OV-Watch Gateway - Bluetooth mode (HC-05 SPP @ 9600)
rem
rem  Double-click this file. It is the same as:
rem      start_gateway.cmd -Bt
rem
rem  Before running: HC-05 must be paired in Windows
rem  (Settings > Bluetooth & devices). Pairing alone is not enough -
rem  the link is only established when a program opens the COM port,
rem  which is exactly what this launcher does.
rem
rem  Keep the USB serial monitor open at the same time: the board
rem  prints [bt] ... lines there so you can confirm the link.
rem ============================================================
title OV-Watch Gateway - Bluetooth
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_gateway.ps1" -Bt %*
echo.
pause
