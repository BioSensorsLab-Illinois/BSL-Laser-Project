@echo off
REM BSL Host Console — Windows launcher.
REM Double-click this file in File Explorer to start the console.
REM
REM PowerShell 5+ ships with Windows 10 and 11 — nothing to install.

setlocal
cd /d "%~dp0"

powershell.exe -ExecutionPolicy Bypass -NoProfile -File "server\serve.ps1" %*

if errorlevel 1 (
  echo.
  echo Server exited with error %errorlevel%. Press any key to close.
  pause >nul
)
endlocal
