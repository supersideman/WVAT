@echo off
rem ============================================================
rem  WVAT - push local history to GitHub
rem  Double-click this file.
rem
rem  TLS note (why this repo is configured for the openssl backend):
rem    schannel dies here with SEC_E_NO_REVOCATION_CHECK (0x80092012)
rem    because the revocation endpoint is unreachable through the
rem    network proxy. The openssl backend plus the exported Windows
rem    root store (.git\win-ca.pem) gets verified TLS through the same
rem    proxy, so both settings are stored in this repo's git config.
rem ============================================================
title WVAT - push to GitHub
cd /d "%~dp0.."

echo ================================================
echo   WVAT - push to GitHub
echo ================================================
echo.

where git >nul 2>nul
if errorlevel 1 goto nogit

if not exist ".git\win-ca.pem" goto noca

echo [1/4] TLS settings in use:
git config --get http.sslBackend
git config --get http.sslCAInfo
echo.
echo [2/4] remote:
git remote -v
echo.
echo [3/4] pushing main branch (95 commits) ...
git push -u origin main
if errorlevel 1 goto fail

echo.
echo [4/4] pushing tags (10 tags) ...
git push origin --tags
if errorlevel 1 goto fail

echo.
echo ================================================
echo   DONE. Open: https://github.com/supersideman/WVAT
echo   Expect: 95 commits, 10 tags
echo ================================================
echo.
pause
exit /b 0

:fail
echo.
echo ================================================
echo   FAILED - read the messages above.
echo.
echo   TLS / certificate errors: the two settings below must exist
echo     git config http.sslBackend openssl
echo     git config http.sslCAInfo "%CD%\.git\win-ca.pem"
echo.
echo   Login / auth errors (GCM, browser, 401, 403):
echo     tell the assistant - we will switch to a stored token.
echo.
echo   rejected / non-fast-forward: remote is NOT empty.
echo   Tell the assistant, do NOT force push.
echo ================================================
echo.
pause
exit /b 1

:noca
echo [ERROR] .git\win-ca.pem is missing.
echo Ask the assistant to export the Windows root store again.
echo.
pause
exit /b 1

:nogit
echo [ERROR] git not found in PATH.
echo Install Git for Windows, then open a NEW window and retry.
echo.
pause
exit /b 1
