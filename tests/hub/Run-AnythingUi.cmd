@echo off
set "NH_UI_PATH=%PATH%"
set PATH=
set "Path=%NH_UI_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\hub-anything\ui mkdir builds\hub-anything\ui
cl /nologo /O2 /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Iexternal\nlohmann tests\hub\AnythingProtocolFixture.cpp /Fobuilds\hub-anything\ui\ /Febuilds\hub-anything\ui\AnythingProtocolFixture.exe /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /O2 /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DIMGUI_ENABLE_TEST_ENGINE /DIMGUI_USER_CONFIG=\"HubImConfig.h\" /Iexternal\nlohmann /Iapps\NeuRoticHub /Iapps\NeuRoticHub\library /IOptiScaler\include /IOptiScaler\include\imgui tests\hub\AnythingUiTests.cpp apps\NeuRoticHub\anything\AnythingView.cpp apps\NeuRoticHub\anything\AnythingController.cpp apps\NeuRoticHub\library\ManualLibrary.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\imgui_widgets.cpp /Fobuilds\hub-anything\ui\ /Febuilds\hub-anything\ui\AnythingUiTests.exe /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp /link shell32.lib user32.lib ole32.lib comdlg32.lib
if errorlevel 1 exit /b %ERRORLEVEL%
builds\hub-anything\ui\AnythingUiTests.exe
exit /b %ERRORLEVEL%
