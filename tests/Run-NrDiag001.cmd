@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Candidate entry point. Initialize the verified Visual Studio x64 environment separately.
rem Evidence output must be an explicit directory outside the implementation checkout.
if "%~1"=="" (
  echo Usage: Run-NrDiag001.cmd ^<new-evidence-directory-outside-checkout^> [--benchmark]
  exit /b 2
)
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" (
  echo Initialize the verified Visual Studio x64 environment before this runner.
  exit /b 2
)
set "nrDiagOutput=%~f1"
if not exist "%nrDiagOutput%" mkdir "%nrDiagOutput%"
if errorlevel 1 exit /b 1

pushd "%~dp0.."
if errorlevel 1 exit /b 1

python tests\nr\diagnostics\diag001_source_guard.py > "%nrDiagOutput%\nr_diag_source_guard.log" 2>&1
if errorlevel 1 goto failed

cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler tests\nr\diagnostics\nr_diag_m0.cpp /Fe:"%nrDiagOutput%\nr_diag_m0_test.exe" /Fo:"%nrDiagOutput%\nr_diag_m0_test.obj" > "%nrDiagOutput%\nr_diag_m0_build.log" 2>&1
if errorlevel 1 goto failed
"%nrDiagOutput%\nr_diag_m0_test.exe" > "%nrDiagOutput%\nr_diag_m0_run.log" 2>&1
if errorlevel 1 goto failed

cl /nologo /std:c++20 /utf-8 /EHsc /W4 /DSPDLOG_USE_STD_FORMAT /I OptiScaler /I external\spdlog\include tests\nr\diagnostics\nr_diag_frame_trace.cpp /Fe:"%nrDiagOutput%\nr_diag_frame_trace_test.exe" /Fo:"%nrDiagOutput%\nr_diag_frame_trace_test.obj" > "%nrDiagOutput%\nr_diag_frame_trace_build.log" 2>&1
if errorlevel 1 goto failed
for %%m in (off shadow) do (
  "%nrDiagOutput%\nr_diag_frame_trace_test.exe" %%m > "%nrDiagOutput%\nr_diag_frame_trace_%%m.log" 2>&1
  if errorlevel 1 goto failed
)

cl /nologo /std:c++20 /EHsc /O2 /W4 /I OptiScaler tests\nr\diagnostics\nr_diag_t48.cpp /Fe:"%nrDiagOutput%\nr_diag_t48.exe" /Fo:"%nrDiagOutput%\nr_diag_t48.obj" > "%nrDiagOutput%\nr_diag_t48_build.log" 2>&1
if errorlevel 1 goto failed
if /I "%~2"=="--benchmark" (
  "%nrDiagOutput%\nr_diag_t48.exe" > "%nrDiagOutput%\nr_diag_t48.csv" 2>&1
  if errorlevel 1 goto failed
) else (
  > "%nrDiagOutput%\nr_diag_t48_NOT_RUN.txt" echo T48 executable prepared; benchmark NOT_RUN unless --benchmark is supplied.
)

popd
exit /b 0

:failed
set "nrDiagExit=%ERRORLEVEL%"
popd
exit /b %nrDiagExit%
