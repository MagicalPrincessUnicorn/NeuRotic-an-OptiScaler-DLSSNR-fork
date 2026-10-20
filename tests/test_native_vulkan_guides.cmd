@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-NativeVulkanGuides.ps1"
exit /b %errorlevel%
