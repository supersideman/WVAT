@echo off
rem Show recent commits in a plain console window (no gitk needed).
title WVAT - recent commits
cd /d "%~dp0.."
git --no-pager log --oneline --decorate -25
echo.
echo ---- tags ----
git --no-pager tag -l
echo.
pause