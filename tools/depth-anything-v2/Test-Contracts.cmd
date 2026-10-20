@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\depth-anything-v2\contracts mkdir builds\depth-anything-v2\contracts
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 tests\window-worker\depth-anything-v2\DepthContractTests.cpp /Fobuilds\depth-anything-v2\contracts\DepthContractTests.obj /Febuilds\depth-anything-v2\contracts\DepthContractTests.exe
if errorlevel 1 exit /b %ERRORLEVEL%
builds\depth-anything-v2\contracts\DepthContractTests.exe
exit /b %ERRORLEVEL%
