@echo off
setlocal
echo NeuRotic - existing Crimson Desert crash information
echo This reads existing Windows crash records. It does not start the game or change settings.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Collect-MfgCrash.ps1" -OutputDirectory "%~dp0."
if errorlevel 1 echo Collection failed. Copy the error above back to the chat.
pause
