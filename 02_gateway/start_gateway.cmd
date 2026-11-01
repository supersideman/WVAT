@echo off
rem OV-Watch Gateway launcher
title OV-Watch Gateway
cd /d "%~dp0"

set "PY=C:\Users\???\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"
if not exist "%PY%" set "PY=python"

echo ================================================
echo   OV-Watch Gateway  (Ctrl+C or close to stop)
echo ================================================
echo.

"%PY%" run_gateway.py %*

echo.
echo Gateway exited. Press any key to close...
pause >nul
