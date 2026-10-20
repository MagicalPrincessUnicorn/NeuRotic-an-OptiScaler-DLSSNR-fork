@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "NRPG_ROOT=%~dp0..\.."
set "NRPG_OUT=%NRPG_ROOT%\builds\prepared-guides"
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /O2 /DNOMINMAX /I"%~dp0..\..\OptiScaler" /I"%~dp0external\reshade\include" "%~dp0src\addon_registry_smoke.cpp" /Fo"%NRPG_OUT%\addon_registry_smoke.obj" /Fe"%NRPG_OUT%\addon_registry_smoke.exe"
if errorlevel 1 exit /b 1
"%NRPG_OUT%\addon_registry_smoke.exe" "%NRPG_OUT%\NeuRotic-PreparedGuides.addon64"
exit /b %errorlevel%
