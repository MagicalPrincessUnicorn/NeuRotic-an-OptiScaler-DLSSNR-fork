@echo off
set "NH_HOTKEY_PATH=%PATH%"
set PATH=
set "Path=%NH_HOTKEY_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\hub-anything mkdir builds\hub-anything
cl /nologo /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Iexternal\nlohmann /Iapps\NeuRoticHub tests\hub\AnythingHotkeyTests.cpp /Fobuilds\hub-anything\hotkey.obj /Febuilds\hub-anything\AnythingHotkeyTests.exe /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp /link user32.lib ole32.lib
if errorlevel 1 exit /b %ERRORLEVEL%
builds\hub-anything\AnythingHotkeyTests.exe
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /EHsc /std:c++20 /DUNICODE /D_UNICODE /DNOMINMAX /Iapps\NeuRoticWindowWorker tests\window-worker\InteractionPolicyTests.cpp /Fobuilds\hub-anything\interaction.obj /Febuilds\hub-anything\InteractionPolicyTests.exe
if errorlevel 1 exit /b %ERRORLEVEL%
builds\hub-anything\InteractionPolicyTests.exe
exit /b %ERRORLEVEL%
