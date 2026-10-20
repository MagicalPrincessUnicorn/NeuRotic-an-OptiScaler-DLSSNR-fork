@echo off
setlocal
set "NH_KEYBIND_PATH=%PATH%"
set PATH=
set "Path=%NH_KEYBIND_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
if not exist builds\hub\hotkey-chord mkdir builds\hub\hotkey-chord
cl /nologo /O2 /std:c++20 /EHsc /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DNH_KEYBIND_UI_STANDALONE /DIMGUI_ENABLE_TEST_ENGINE /DIMGUI_USER_CONFIG=\"HubImConfig.h\" /Iapps\NeuRoticHub /IOptiScaler\include /IOptiScaler\include\imgui tests\hub\KeybindUiTests.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\imgui_widgets.cpp /Fobuilds\hub\hotkey-chord\ /Febuilds\hub\hotkey-chord\KeybindEditorTests.exe /link user32.lib
if errorlevel 1 exit /b 1
builds\hub\hotkey-chord\KeybindEditorTests.exe
exit /b %ERRORLEVEL%
