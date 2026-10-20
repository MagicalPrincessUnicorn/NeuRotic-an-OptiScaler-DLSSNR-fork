@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "OUT=builds\moonshot-w03-reconciliation\status-anchor"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /W3 /I tests\opticlip_stubs /I OptiScaler /I OptiScaler\include /I OptiScaler\include\imgui /I external\freetype tests\nr_status_anchor.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%OUT%\status-anchor.exe" /Fo:"%OUT%\\" /link user32.lib shell32.lib external\freetype\freetype.lib
if errorlevel 1 exit /b 1
"%OUT%\status-anchor.exe"
exit /b %errorlevel%
