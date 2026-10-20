@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "previewOut=%~dp0..\builds\ui-release\sleek"
if not exist "%previewOut%" mkdir "%previewOut%"
pushd "%~dp0.."
if exist "%previewOut%\preview.txt" del "%previewOut%\preview.txt"
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I OptiScaler\include\imgui /I external\freetype /I external\nlohmann tests\sleek_ui_preview.cpp OptiScaler\menu\BrandImage.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\menu\LocalizationFonts.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\imgui_impl_dx11.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%previewOut%\sleek-preview.exe" /Fo:"%previewOut%\\" /link d3d11.lib d3dcompiler.lib dxgi.lib user32.lib external\freetype\freetype.lib >"%previewOut%\preview-build.txt" 2>&1
if not "%ERRORLEVEL%"=="0" goto failed
"%previewOut%\sleek-preview.exe" >"%previewOut%\preview.txt" 2>&1
if not "%ERRORLEVEL%"=="0" goto failed
type "%previewOut%\preview.txt"
popd
exit /b 0
:failed
set "previewResult=%ERRORLEVEL%"
type "%previewOut%\preview-build.txt"
if exist "%previewOut%\preview.txt" type "%previewOut%\preview.txt"
popd
exit /b %previewResult%
