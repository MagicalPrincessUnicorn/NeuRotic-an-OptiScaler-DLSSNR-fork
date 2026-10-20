@echo off
setlocal
cd /d "%~dp0..\.."
set "OUT=builds\universal-input\pipeline-tests"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
for %%T in (nr_diagnostic_signals nr_signal_receipts nr_preflight_signals) do (
 cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\%%T.cpp /Fo"%OUT%\\" /Fe"%OUT%\%%T.exe" /link /INCREMENTAL:NO
 if errorlevel 1 exit /b 1
 "%OUT%\%%T.exe"
 if errorlevel 1 exit /b 1
)
exit /b 0
