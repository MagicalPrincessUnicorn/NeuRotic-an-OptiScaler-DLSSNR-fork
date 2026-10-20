@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\Start-CandidateCapture.ps1" -Mode On %*
pause
