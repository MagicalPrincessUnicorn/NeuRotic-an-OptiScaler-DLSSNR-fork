@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "testOut=%~dp0..\builds\ui-release\sleek"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%~dp0.."
if exist "%testOut%\test.txt" del "%testOut%\test.txt"
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I external\freetype tests\sleek_ui_test.cpp OptiScaler\menu\BrandImage.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%testOut%\sleek-ui-test.exe" /Fo:"%testOut%\\" /link external\freetype\freetype.lib >"%testOut%\build.txt" 2>&1
if not "%ERRORLEVEL%"=="0" goto failed
"%testOut%\sleek-ui-test.exe" >"%testOut%\test.txt" 2>&1
if not "%ERRORLEVEL%"=="0" goto failed
type "%testOut%\test.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
type "%testOut%\build.txt"
if exist "%testOut%\test.txt" type "%testOut%\test.txt"
popd
exit /b %testResult%
