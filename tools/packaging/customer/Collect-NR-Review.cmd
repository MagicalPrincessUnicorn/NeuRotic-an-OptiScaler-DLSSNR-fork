@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\Collect-NR-Review.ps1" %*
pause
