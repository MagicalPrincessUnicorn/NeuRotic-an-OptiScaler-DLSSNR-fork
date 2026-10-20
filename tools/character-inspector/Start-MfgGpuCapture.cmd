@echo off
setlocal
echo NeuRotic - Crimson Desert GPU crash capture
echo Fully exit the game and Steam first. Installed files and saved settings are preserved.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0Start-MfgGpuCapture.ps1" -OutputDirectory "%~dp0."
if errorlevel 1 echo Capture failed. Send the error above to the chat.
pause
