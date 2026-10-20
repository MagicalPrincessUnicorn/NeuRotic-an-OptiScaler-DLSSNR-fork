@echo off
setlocal
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" exit /b 1
set "CI_COLOR_ROOT=%~dp0.."
set "CI_COLOR_OUT=%CI_COLOR_ROOT%\builds\character-inspector\tests\color"
if not exist "%CI_COLOR_OUT%" mkdir "%CI_COLOR_OUT%"
pushd "%CI_COLOR_ROOT%"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I external\freetype tests\nr\semantic\character_color_tests.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%CI_COLOR_OUT%\character_color.exe" /Fo:"%CI_COLOR_OUT%\\" /link user32.lib external\freetype\freetype.lib >"%CI_COLOR_OUT%\build.txt" 2>&1
if errorlevel 1 (type "%CI_COLOR_OUT%\build.txt" & popd & exit /b 1)
"%CI_COLOR_OUT%\character_color.exe" >"%CI_COLOR_OUT%\test.txt" 2>&1
if errorlevel 1 (type "%CI_COLOR_OUT%\test.txt" & popd & exit /b 1)
type "%CI_COLOR_OUT%\test.txt"
popd
exit /b 0
