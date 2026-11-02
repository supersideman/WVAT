@echo off
rem Open the WVAT repository history in gitk.
rem Run by double-clicking THIS file. It always switches to the repo root
rem first, so gitk never starts in a non-repository directory.
title WVAT - git history
cd /d "%~dp0.."

git rev-parse --show-toplevel >nul 2>&1
if errorlevel 1 (
  echo.
  echo [error] this folder is not a git repository:
  echo         %CD%
  echo.
  echo         expected a .git folder in: %~dp0..
  pause
  exit /b 1
)

echo [info] repository: %CD%
start "" gitk --all
exit /b 0