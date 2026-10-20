@echo off
setlocal
set "NR_EF_ROOT=%~dp0..\.."
set "NR_EF_ARCH=%~1"
if "%NR_EF_ARCH%"=="" set "NR_EF_ARCH=x64"
set "NR_EF_EXT=addon64"
if "%NR_EF_ARCH%"=="x86" (set "NR_EF_EXT=addon32"&call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" -vcvars_ver=14.44) else (call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44)
if errorlevel 1 exit /b 1
set "NR_EF_OUT=%NR_EF_ROOT%\builds\universal-connections\external-feeder\%NR_EF_ARCH%"
if not exist "%NR_EF_OUT%" mkdir "%NR_EF_OUT%"
cl /nologo /std:c++20 /EHsc /MD /O2 /W4 /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /I"%NR_EF_ROOT%\OptiScaler" /I"%~dp0..\prepared-guides\external\reshade\include" /LD "%~dp0src\feed_neurotic_prepared.cpp" /Fo"%NR_EF_OUT%\\" /Fe"%NR_EF_OUT%\NeuRotic-ExternalFeeder.%NR_EF_EXT%" /link /IMPLIB:"%NR_EF_OUT%\NeuRotic-ExternalFeeder.lib"
exit /b %errorlevel%
