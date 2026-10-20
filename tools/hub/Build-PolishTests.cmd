@echo off
set "NH_POLISH_PATH=%PATH%"
set PATH=
set "Path=%NH_POLISH_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\hub-polish mkdir builds\hub-polish
cl /nologo /std:c++20 /EHsc /MT /utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DNH_NUMERIC_TEST_MAIN /Iapps\NeuRoticHub /Iapps\NeuRoticHub\library /IOptiScaler\include /Iexternal\nlohmann tests\hub\NumericSettingsTests.cpp /Febuilds\hub-polish\numeric-tests.exe /Fobuilds\hub-polish\numeric-tests.obj /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
exit /b %ERRORLEVEL%
