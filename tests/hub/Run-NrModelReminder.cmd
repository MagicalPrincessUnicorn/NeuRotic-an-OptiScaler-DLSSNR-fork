@echo off
set "NR_REMINDER_PATH=%PATH%"
set PATH=
set "Path=%NR_REMINDER_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\hub\nr-model-reminder mkdir builds\hub\nr-model-reminder
cl /nologo /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Iexternal\nlohmann /Iapps\NeuRoticHub tests\hub\NrModelReminderTests.cpp /Fobuilds\hub\nr-model-reminder\tests.obj /Febuilds\hub\nr-model-reminder\NrModelReminderTests.exe /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
if errorlevel 1 exit /b %ERRORLEVEL%
builds\hub\nr-model-reminder\NrModelReminderTests.exe
exit /b %ERRORLEVEL%
