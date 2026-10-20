@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
set "sectionOut=builds\ui-release\section-header"
if not exist "%sectionOut%" mkdir "%sectionOut%"
cl /nologo /std:c++20 /EHsc /utf-8 /O2 /I OptiScaler /I OptiScaler\include /I external\freetype tests\menu_section_header_tests.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fo:"%sectionOut%\\" /Fe:"%sectionOut%\tests.exe" /link external\freetype\freetype.lib >"%sectionOut%\build.log" 2>&1
if errorlevel 1 (type "%sectionOut%\build.log" & popd & exit /b 1)
"%sectionOut%\tests.exe"
set "sectionResult=%errorlevel%"
popd
exit /b %sectionResult%
