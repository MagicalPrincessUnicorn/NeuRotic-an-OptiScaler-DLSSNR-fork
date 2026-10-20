@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\depth-anything-v2\app mkdir builds\depth-anything-v2\app
cl /nologo /std:c++20 /EHsc /O2 /DNOMINMAX /DUNICODE /D_UNICODE /Iexternal\nlohmann /DNH_DEPTH_PROTOCOL tests\hub\AnythingProtocolFixture.cpp /Fobuilds\depth-anything-v2\app\fixture.obj /Febuilds\depth-anything-v2\app\DepthProtocolFixture.exe
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /std:c++20 /EHsc /O2 /DNOMINMAX /DUNICODE /D_UNICODE /Iexternal\nlohmann tests\hub\AnythingProtocolFixture.cpp /Fobuilds\depth-anything-v2\app\old.obj /Febuilds\depth-anything-v2\app\OldDepthProtocolFixture.exe
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /DNOMINMAX /DUNICODE /D_UNICODE /Iexternal\nlohmann /Iapps\NeuRoticHub tests\window-worker\depth-anything-v2\AppDepthTests.cpp apps\NeuRoticHub\anything\AnythingController.cpp apps\NeuRoticHub\library\ManualLibrary.cpp /Fobuilds\depth-anything-v2\app\ /Febuilds\depth-anything-v2\app\AppDepthTests.exe /link shell32.lib user32.lib ole32.lib
if errorlevel 1 exit /b %ERRORLEVEL%
builds\depth-anything-v2\app\AppDepthTests.exe
exit /b %ERRORLEVEL%
