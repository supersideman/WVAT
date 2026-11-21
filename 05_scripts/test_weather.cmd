@echo off
rem ============================================================
rem  Weather self-test -- run this if the watch page shows "--"
rem
rem  It calls the same code the gateway uses, so the result tells
rem  us exactly why the watch has no weather:
rem    * fetch ok      -> network fine, problem is elsewhere
rem    * fetch failed  -> network/blocked, see the error text
rem ============================================================
title Weather self-test
set PY=%USERPROFILE%\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe
cd /d "%~dp0.."
echo.
echo Testing weather fetch (open-meteo)...
echo.
"%PY%" -c "import sys; sys.path.insert(0,'02_gateway'); import weather as W; w=W.Weather(); ok=w.update(0); print(); print('  ok =', ok); print('  fields =', w.frame_fields()); print('  error  =', w.last_error or '(none)'); print('  city   =', w.city, '(', w.lat, ',', w.lon, ')')"
echo.
echo If ok is False, tell the assistant the exact error text above.
echo.
pause
