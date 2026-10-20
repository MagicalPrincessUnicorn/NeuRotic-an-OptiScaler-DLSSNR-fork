@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
set "CI_OUT=builds\character-inspector\tests"
set "CI_UI=%CI_OUT%\imgui"
if not exist "%CI_UI%" mkdir "%CI_UI%"
cl /nologo /std:c++20 /EHsc /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I external\freetype /I external\nlohmann tests\nr\semantic\character_imgui_tests.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%CI_UI%\character_imgui.exe" /Fo:"%CI_UI%\\" /link user32.lib external\freetype\freetype.lib >"%CI_UI%\build.txt" 2>&1
if errorlevel 1 (type "%CI_UI%\build.txt" & popd & exit /b 1)
"%CI_UI%\character_imgui.exe" >"%CI_UI%\test.txt" 2>&1
if errorlevel 1 (type "%CI_UI%\test.txt" & popd & exit /b 1)
type "%CI_UI%\test.txt"
popd
exit /b 0
