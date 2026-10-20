@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
pushd "%~dp0.."
if not exist builds\candidate-observer mkdir builds\candidate-observer
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_candidate_observer.cpp /Fe:builds\candidate-observer\core.exe /Fo:builds\candidate-observer\core.obj
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
builds\candidate-observer\core.exe
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_candidate_capture.cpp OptiScaler\nr\diagnostics\candidate\Observer.cpp /Fe:builds\candidate-observer\capture.exe /Fo:builds\candidate-observer\
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
if "%~1"=="" goto complete
builds\candidate-observer\capture.exe "%~1"
:complete
set "result=%ERRORLEVEL%"
popd
exit /b %result%
