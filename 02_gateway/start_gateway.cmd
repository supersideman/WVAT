@echo off
rem OV-Watch Gateway launcher
title OV-Watch Gateway
cd /d "%~dp0"

rem NOTE: never hardcode the user profile path in this file.
rem   The Windows user folder here contains non-ASCII characters; writing it
rem   into an ASCII .cmd turns it into "???" and the path silently breaks.
rem   Expand it at runtime instead.
set "PY="
if exist "%USERPROFILE%\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe" set "PY=%USERPROFILE%\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"

if not defined PY (
  echo [launcher] ERROR: bundled Python 3.12 not found.
  echo            expected: %%USERPROFILE%%\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe
  echo.
  echo   Do NOT fall back to a system python: the C extensions (cffi, vosk)
  echo   are built for cp312 only.
  pause
  exit /b 1
)

echo ================================================
echo   OV-Watch Gateway   Ctrl+C or close to stop
echo ================================================
echo.
echo [launcher] python: %PY%
"%PY%" -c "import sys; print('[launcher] version', sys.version.split()[0])"
echo.

"%PY%" run_gateway.py %*

echo.
echo Gateway exited. Press any key to close...
pause >nul