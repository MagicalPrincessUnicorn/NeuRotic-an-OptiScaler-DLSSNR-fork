@echo off
setlocal
title NeuRotic Uninstaller
echo NeuRotic Uninstaller
echo Select the executable for the game where NeuRotic is installed.
echo The uninstaller will detect the managed proxy, files, settings and restore history.
echo.
set "PSModulePath="
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\NeuRotic-Setup-Engine.ps1" -Uninstall %*
set "result=%ERRORLEVEL%"
if not "%result%"=="0" echo Uninstall did not complete. Read the message above.
pause
exit /b %result%
