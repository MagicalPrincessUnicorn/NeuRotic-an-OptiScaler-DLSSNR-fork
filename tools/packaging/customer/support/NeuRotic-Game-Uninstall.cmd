@echo off
setlocal
rem End batch-file reading before the child removes this owned launcher.
(goto) 2>nul & "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0NeuRotic\Installer\NeuRotic-Uninstall.ps1" && (pause & exit /b 0) || (pause & exit /b 2)
