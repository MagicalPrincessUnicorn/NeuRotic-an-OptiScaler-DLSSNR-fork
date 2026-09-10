@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-MultipassTest.ps1" %*
set "result=%ERRORLEVEL%"
if not "%result%"=="0" echo Installation did not complete. See the message above.
pause
exit /b %result%
