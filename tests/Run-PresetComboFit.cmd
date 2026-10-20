@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "presetFitOut=%~dp0..\builds\ui-release\preset-fit"
if not exist "%presetFitOut%" mkdir "%presetFitOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /I OptiScaler /I OptiScaler\include /I external\nlohmann /I external\freetype tests\preset_combo_fit.cpp OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%presetFitOut%\tests.exe" /Fo:"%presetFitOut%\\" /link external\freetype\freetype.lib >"%presetFitOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%presetFitOut%\tests.exe"
set "presetFitResult=%errorlevel%"
popd
exit /b %presetFitResult%
:failed
type "%presetFitOut%\build.txt"
popd
exit /b 1
