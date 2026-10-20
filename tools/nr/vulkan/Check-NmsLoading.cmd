@echo off
setlocal
echo Leave No Man's Sky running at its main menu, then run this check.
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Check-NmsLoading.ps1" %*
if errorlevel 1 echo The check failed. Keep the message above for diagnosis.
pause
