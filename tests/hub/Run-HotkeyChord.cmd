@echo off
setlocal
set "NH_CHORD_PATH=%PATH%"
set PATH=
set "Path=%NH_CHORD_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
if not exist builds\hub\hotkey-chord mkdir builds\hub\hotkey-chord
cl /nologo /EHsc /std:c++20 /utf-8 tests\hub\HotkeyChordTests.cpp /Fobuilds\hub\hotkey-chord\HotkeyChordTests.obj /Febuilds\hub\hotkey-chord\HotkeyChordTests.exe /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
if errorlevel 1 exit /b 1
builds\hub\hotkey-chord\HotkeyChordTests.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++20 /utf-8 /DNH_KEYBIND_STANDALONE /Iapps\NeuRoticHub tests\hub\KeybindCaptureTests.cpp /Fobuilds\hub\hotkey-chord\KeybindCaptureTests.obj /Febuilds\hub\hotkey-chord\KeybindCaptureTests.exe /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
if errorlevel 1 exit /b 1
builds\hub\hotkey-chord\KeybindCaptureTests.exe
exit /b %ERRORLEVEL%
