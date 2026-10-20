@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "testOut=%~dp0..\builds\ui-release\sleek"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%~dp0.."
if exist "%testOut%\water-test.txt" del "%testOut%\water-test.txt"
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I external\freetype tests\water_transition_test.cpp OptiScaler\menu\BrandImage.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%testOut%\water-transition-test.exe" /Fo:"%testOut%\\" /link external\freetype\freetype.lib >"%testOut%\water-build.txt" 2>&1
if not "%ERRORLEVEL%"=="0" goto failed
"%testOut%\water-transition-test.exe" >"%testOut%\water-test.txt" 2>&1
if not "%ERRORLEVEL%"=="0" goto failed
type "%testOut%\water-test.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
type "%testOut%\water-build.txt"
if exist "%testOut%\water-test.txt" type "%testOut%\water-test.txt"
popd
exit /b %testResult%

