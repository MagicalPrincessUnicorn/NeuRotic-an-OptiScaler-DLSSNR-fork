@echo off
setlocal
set "PSModulePath="
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\Start-PreparedDepth-Test.ps1" Baseline %*
exit /b %errorlevel%
