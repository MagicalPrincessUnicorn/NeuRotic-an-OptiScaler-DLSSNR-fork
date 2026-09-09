@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "testOut=C:\OptiScaler-NR-Dev\logs\neurotic-localization"
if not exist "%testOut%" mkdir "%testOut%"
if not exist "%testOut%" exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I external\freetype tests\localization_test.cpp OptiScaler\menu\Localization.cpp OptiScaler\menu\LocalizationFonts.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%testOut%\localization.exe" /Fo:"%testOut%\\" /link user32.lib shell32.lib external\freetype\freetype.lib >"%testOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\localization.exe" >"%testOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\test.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
type "%testOut%\build.txt"
if exist "%testOut%\test.txt" type "%testOut%\test.txt"
popd
exit /b %testResult%
