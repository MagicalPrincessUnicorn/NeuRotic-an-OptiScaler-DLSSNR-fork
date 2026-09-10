@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore-MultipassTest.ps1"
set "result=%ERRORLEVEL%"
pause
exit /b %result%
