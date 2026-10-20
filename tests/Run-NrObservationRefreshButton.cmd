@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "NR_BUTTON_OUT=builds\ui-release\observation-button"
if not exist "%NR_BUTTON_OUT%" mkdir "%NR_BUTTON_OUT%"
cl /nologo /std:c++20 /EHsc /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I external\freetype tests\nr_observation_refresh_button.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%NR_BUTTON_OUT%\test.exe" /Fo:"%NR_BUTTON_OUT%\\" /link external\freetype\freetype.lib >"%NR_BUTTON_OUT%\build.txt" 2>&1
if errorlevel 1 exit /b 1
"%NR_BUTTON_OUT%\test.exe" >"%NR_BUTTON_OUT%\result.txt" 2>&1
set "NR_BUTTON_RESULT=%errorlevel%"
type "%NR_BUTTON_OUT%\result.txt"
exit /b %NR_BUTTON_RESULT%
