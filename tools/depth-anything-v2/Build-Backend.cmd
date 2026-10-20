@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\depth-anything-v2\backend mkdir builds\depth-anything-v2\backend
cl /nologo /LD /std:c++20 /EHsc /W4 /WX /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I_deps\depth-anything-v2\cuda-12.9\Library\include /I_deps\depth-anything-v2\tensorrt-10.13\include apps\NeuRoticWindowWorker\guidance\dav2\Dav2Backend.cpp /Fobuilds\depth-anything-v2\backend\Dav2Backend.obj /Febuilds\depth-anything-v2\backend\NeuRotic.DepthAnything.dll /link bcrypt.lib version.lib
exit /b %ERRORLEVEL%
