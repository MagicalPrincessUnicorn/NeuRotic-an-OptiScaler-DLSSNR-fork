@echo off
setlocal
title NeuRotic Setup
echo NeuRotic Setup
echo Select your game. Setup will show the destination and settings before installing.
echo.
set "PSModulePath="
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\NeuRotic-Setup-Engine.ps1" %*
set "result=%ERRORLEVEL%"
if not "%result%"=="0" echo Setup did not complete. Read the message above.
pause
exit /b %result%
