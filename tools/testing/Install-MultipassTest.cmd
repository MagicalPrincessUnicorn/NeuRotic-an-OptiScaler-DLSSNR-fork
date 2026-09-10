@echo off
setlocal
rem Rebuild the Windows PowerShell module path if launched from PowerShell 7.
set "PSModulePath="
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-MultipassTest.ps1" %*
set "result=%ERRORLEVEL%"
if not "%result%"=="0" echo Installation did not complete. See the message above.
pause
exit /b %result%
