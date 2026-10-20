@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\inspector-motion\dx11-tests"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /O2 /W4 /DCHARACTER_CAPTURE_TEST /DNOMINMAX /I OptiScaler tests\nr\semantic\character_capture_dx11_tests.cpp OptiScaler\nr\semantic\character\CharacterCaptureDx11.cpp /Fe:"%OUT%\capture.exe" /Fo:"%OUT%\\" /link d3d11.lib d3d12.lib dxgi.lib user32.lib >"%OUT%\build.log" 2>&1
if errorlevel 1 (type "%OUT%\build.log" & popd & exit /b 1)
"%OUT%\capture.exe" >"%OUT%\test.log" 2>&1
set "RESULT=%ERRORLEVEL%"
type "%OUT%\test.log"
popd
exit /b %RESULT%
