@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Candidate entry point. Initialize the verified Visual Studio x64 environment separately.
rem Evidence output must be an explicit directory outside the implementation checkout.
if "%~1"=="" (
  echo Usage: Run-NrCfg001.cmd ^<new-evidence-directory-outside-checkout^>
  exit /b 2
)
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo Initialize the verified Visual Studio x64 environment before this runner.
  exit /b 2
)
set "nrCfgOutput=%~f1"
if not exist "%nrCfgOutput%" mkdir "%nrCfgOutput%"
if errorlevel 1 exit /b 1

pushd "%~dp0.."
if errorlevel 1 exit /b 1
python tests\nr\config\cfg001_source_guard.py > "%nrCfgOutput%\nr_cfg001_source_guard.log" 2>&1
if errorlevel 1 goto failed
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler tests\nr\config\nr_cfg001.cpp /Fe:"%nrCfgOutput%\nr_cfg001_test.exe" /Fo:"%nrCfgOutput%\nr_cfg001_test.obj" > "%nrCfgOutput%\nr_cfg001_build.log" 2>&1
if errorlevel 1 goto failed
"%nrCfgOutput%\nr_cfg001_test.exe" > "%nrCfgOutput%\nr_cfg001_run.log" 2>&1
if errorlevel 1 goto failed
popd
exit /b 0

:failed
set "nrCfgExit=%ERRORLEVEL%"
popd
exit /b %nrCfgExit%
