@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\depth-anything-v2\module mkdir builds\depth-anything-v2\module
set "SOURCES="
if exist apps\NeuRoticWindowWorker\guidance\dav2\Dav2Module.cpp set "SOURCES=apps\NeuRoticWindowWorker\guidance\dav2\Dav2Module.cpp"
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Iexternal\nlohmann tests\window-worker\depth-anything-v2\ModuleTests.cpp %SOURCES% /Fobuilds\depth-anything-v2\module\ /Febuilds\depth-anything-v2\module\ModuleTests.exe /link bcrypt.lib
if errorlevel 1 exit /b %ERRORLEVEL%
builds\depth-anything-v2\module\ModuleTests.exe
exit /b %ERRORLEVEL%
