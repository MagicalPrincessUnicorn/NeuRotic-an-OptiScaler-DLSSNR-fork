@echo off
setlocal
title NeuRotic Manual Setup
echo NeuRotic Manual Setup
echo Select your game, then choose the proxy filename the game should load.
echo For each unfamiliar file, choose Replace, Skip, or Cancel.
echo Skip preserves that file while other files continue installing.
echo Add -FreshInstall to reset NeuRotic settings to the current defaults.
echo.
set "PSModulePath="
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\NeuRotic-Setup-Engine.ps1" %*
set "result=%ERRORLEVEL%"
if not "%result%"=="0" echo Setup did not complete. Read the message above.
pause
exit /b %result%
