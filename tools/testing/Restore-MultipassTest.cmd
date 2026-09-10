@echo off
setlocal
rem Rebuild the Windows PowerShell module path if launched from PowerShell 7.
set "PSModulePath="
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Restore-MultipassTest.ps1"
set "result=%ERRORLEVEL%"
pause
exit /b %result%
