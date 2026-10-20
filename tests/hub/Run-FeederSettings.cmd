@echo off
setlocal
set "NH_FEEDER_PATH=%PATH%"
set PATH=
set "Path=%NH_FEEDER_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
if not exist builds\hub\feeder-ui mkdir builds\hub\feeder-ui
cl /nologo /O2 /std:c++20 /EHsc /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DIMGUI_ENABLE_TEST_ENGINE /DIMGUI_USER_CONFIG=\"HubImConfig.h\" /Iapps\NeuRoticHub /Iapps\NeuRoticHub\library /IOptiScaler\include /IOptiScaler\include\imgui /Iexternal\nlohmann tests\hub\FeederSettingsTests.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\imgui_widgets.cpp /Fobuilds\hub\feeder-ui\ /Febuilds\hub\feeder-ui\FeederSettingsTests.exe /link user32.lib comdlg32.lib bcrypt.lib
if errorlevel 1 exit /b 1
builds\hub\feeder-ui\FeederSettingsTests.exe
exit /b %ERRORLEVEL%
