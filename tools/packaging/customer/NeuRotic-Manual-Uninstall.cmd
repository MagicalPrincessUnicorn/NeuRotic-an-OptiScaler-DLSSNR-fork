@echo off
setlocal
title NeuRotic Manual Uninstall
echo NeuRotic Manual Uninstall
echo Select the executable for the game where NeuRotic is installed.
echo NeuRotic removes its recorded files, including edited files. Unrelated files stay.
echo An explicitly renamed ReShade is returned to dxgi.dll when that name is free.
echo.
set "PSModulePath="
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\NeuRotic-Setup-Engine.ps1" -Uninstall %*
set "result=%ERRORLEVEL%"
if not "%result%"=="0" echo Uninstall did not complete. Read the message above.
pause
exit /b %result%
