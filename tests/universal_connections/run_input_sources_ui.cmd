@echo off
setlocal
cd /d "%~dp0..\.."
set "OUT=builds\universal-input\ui"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
echo 5102 RCDATA "../../../localization/fonts/NotoSansCJKsc-Regular.otf"> "%OUT%\fonts.rc"
echo 5103 RCDATA "../../../localization/fonts/NotoSans-Regular.ttf">> "%OUT%\fonts.rc"
rc /nologo /fo "%OUT%\fonts.res" "%OUT%\fonts.rc"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /DNOMINMAX /I OptiScaler /I OptiScaler\include /I external\freetype /I external\nlohmann tests\universal_connections\input_sources_ui.cpp OptiScaler\dlssnr\connections\ConnectionPolicy.cpp OptiScaler\dlssnr\connections\ConnectionSelection.cpp OptiScaler\menu\Localization.cpp OptiScaler\menu\LocalizationFonts.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\menu\localization\LanguageFonts.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\imgui_impl_dx11.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%OUT%\input-sources-ui.exe" /Fo:"%OUT%\\" /link "%OUT%\fonts.res" external\freetype\freetype.lib d3d11.lib d3dcompiler.lib dxgi.lib windowscodecs.lib ole32.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
"%OUT%\input-sources-ui.exe" docs\implementation-evidence\universal-input-20261008\ui
exit /b %errorlevel%
