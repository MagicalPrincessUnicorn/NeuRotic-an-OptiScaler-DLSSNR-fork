@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem NEW integration-owner wrapper; no change to the legacy robustness runner.
if "%~1"=="" (
  echo Usage: Run-NrTest002.cmd ^<new-evidence-directory-outside-checkout^>
  exit /b 2
)
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo Initialize the verified Visual Studio x64 environment first.
  exit /b 2
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0nr\contracts\Run-NrTest002.ps1" -OutputDirectory "%~1"
set "nrTestExit=%ERRORLEVEL%"
exit /b %nrTestExit%
