@echo off
setlocal
cd /d "%~dp0.."
if "%~1"=="x86" (call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul) else (call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul)
if errorlevel 1 exit /b 1
if not exist builds\prepared-guides\status mkdir builds\prepared-guides\status
cl /nologo /std:c++20 /EHsc /W4 /WX tests\prepared_guide_status.cpp /Fo:builds\prepared-guides\status\status%~1.obj /Fe:builds\prepared-guides\status\status%~1.exe
if errorlevel 1 exit /b 1
builds\prepared-guides\status\status%~1.exe
exit /b %errorlevel%
