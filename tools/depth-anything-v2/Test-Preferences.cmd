@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\depth-anything-v2\preferences mkdir builds\depth-anything-v2\preferences
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Iexternal\nlohmann /Iapps\NeuRoticHub tests\window-worker\depth-anything-v2\PreferenceTests.cpp /Fobuilds\depth-anything-v2\preferences\ /Febuilds\depth-anything-v2\preferences\PreferenceTests.exe
if errorlevel 1 exit /b %ERRORLEVEL%
builds\depth-anything-v2\preferences\PreferenceTests.exe
exit /b %ERRORLEVEL%
