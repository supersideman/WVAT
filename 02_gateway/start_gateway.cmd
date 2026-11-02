@echo off
rem Thin wrapper: all real work is in start_gateway.ps1
rem (PowerShell handles the non-ASCII user profile path correctly)
title OV-Watch Gateway
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start_gateway.ps1" %*
echo.
pause