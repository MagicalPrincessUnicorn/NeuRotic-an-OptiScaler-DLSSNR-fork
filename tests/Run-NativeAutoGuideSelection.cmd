@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist builds\native-guides\selection mkdir builds\native-guides\selection
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests/NativeAutoGuideSelectionTests.cpp OptiScaler/dlssnr/connections/ConnectionPolicy.cpp /Fobuilds/native-guides/selection/ /Febuilds/native-guides/selection/tests.exe
if errorlevel 1 exit /b 1
for %%s in (0 1 2 3 4) do (
 for %%t in (0 1 2) do (
  builds\native-guides\selection\tests.exe %%s %%t 0
  if errorlevel 1 exit /b 1
  builds\native-guides\selection\tests.exe %%s %%t 1
  if errorlevel 1 exit /b 1
 )
)
del /q builds\native-guides\selection\*.obj
