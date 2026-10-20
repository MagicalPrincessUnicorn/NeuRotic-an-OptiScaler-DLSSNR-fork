@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Candidate entry point. Initialize the toolchain separately under the W00 environment workflow.
rem No unsupervised vcvars/compiler bootstrap is launched by this wrapper.
if "%~1"=="" (
  echo Usage: Run-NrContracts.cmd ^<new-evidence-directory-outside-checkout^>
  exit /b 2
)
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo Initialize the verified Visual Studio x64 environment before this runner.
  exit /b 2
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0nr\contracts\Run-NrContracts.ps1" -OutputDirectory "%~1"
set "nrContractExit=%ERRORLEVEL%"
exit /b %nrContractExit%
