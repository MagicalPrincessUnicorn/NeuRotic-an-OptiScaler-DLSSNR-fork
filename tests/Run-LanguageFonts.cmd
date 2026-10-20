@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "fontOut=%~dp0..\builds\ui-release\language-fonts"
if not exist "%fontOut%" mkdir "%fontOut%"
if exist "%fontOut%\test.txt" del "%fontOut%\test.txt"
pushd "%~dp0.."
rc /nologo /fo "%fontOut%\fonts.res" tests\LanguageFontResources.rc >"%fontOut%\build.txt" 2>&1
if errorlevel 1 goto failed
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /DNOMINMAX /I OptiScaler /I OptiScaler\include /I external\nlohmann /I external\freetype tests\LanguageFontTests.cpp OptiScaler\menu\localization\LanguageFonts.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp "%fontOut%\fonts.res" /Fe:"%fontOut%\tests.exe" /Fo:"%fontOut%\\" /link user32.lib shell32.lib external\freetype\freetype.lib >>"%fontOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%fontOut%\tests.exe" >"%fontOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%fontOut%\test.txt"
popd
exit /b 0
:failed
type "%fontOut%\build.txt"
if exist "%fontOut%\test.txt" type "%fontOut%\test.txt"
popd
exit /b 1
