@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-Worker.ps1" %*
exit /b %ERRORLEVEL%
