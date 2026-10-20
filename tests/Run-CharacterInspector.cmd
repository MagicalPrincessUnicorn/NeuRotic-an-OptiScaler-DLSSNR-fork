@echo off
setlocal EnableDelayedExpansion
if not defined VSCMD_ARG_TGT_ARCH (
    echo Run from an x64 Developer Command Prompt.
    exit /b 1
)
if /I not "%VSCMD_ARG_TGT_ARCH%"=="x64" exit /b 1
set "CI_ROOT=%~dp0.."
set "CI_OUT=%CI_ROOT%\builds\character-inspector\tests"
if not exist "%CI_OUT%" mkdir "%CI_OUT%"
pushd "%CI_ROOT%"
if errorlevel 1 exit /b 1
for %%T in (character_reference_core character_contract character_settings character_pipeline character_wire character_source character_hold character_fg_activity character_real_frame) do (
    cl /nologo /std:c++20 /EHsc /W4 /WX /permissive- /utf-8 /I OptiScaler /I external\simpleini /I external\nlohmann tests\nr\semantic\%%T_tests.cpp /Fe:"%CI_OUT%\%%T.exe" /Fo:"%CI_OUT%\%%T.obj" >"%CI_OUT%\%%T-build.txt" 2>&1
    if errorlevel 1 (type "%CI_OUT%\%%T-build.txt" & popd & exit /b 1)
    "%CI_OUT%\%%T.exe" >"%CI_OUT%\%%T-test.txt" 2>&1
    if errorlevel 1 (type "%CI_OUT%\%%T-test.txt" & popd & exit /b 1)
    type "%CI_OUT%\%%T-test.txt"
)
set "CI_UI=%CI_OUT%\imgui"
if not exist "%CI_UI%" mkdir "%CI_UI%"
cl /nologo /std:c++20 /EHsc /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I external\freetype /I external\nlohmann tests\nr\semantic\character_imgui_tests.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:"%CI_UI%\character_imgui.exe" /Fo:"%CI_UI%\\" /link user32.lib external\freetype\freetype.lib >"%CI_UI%\build.txt" 2>&1
if errorlevel 1 (type "%CI_UI%\build.txt" & popd & exit /b 1)
"%CI_UI%\character_imgui.exe" >"%CI_UI%\test.txt" 2>&1
if errorlevel 1 (type "%CI_UI%\test.txt" & popd & exit /b 1)
type "%CI_UI%\test.txt"
popd
exit /b 0
