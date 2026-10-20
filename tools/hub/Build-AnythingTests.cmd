@echo off
set "NH_ANYTHING_PATH=%PATH%"
set PATH=
set "Path=%NH_ANYTHING_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\hub-anything mkdir builds\hub-anything
cl /nologo /O2 /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Iexternal\nlohmann tests\hub\AnythingProtocolFixture.cpp /Fobuilds\hub-anything\fixture.obj /Febuilds\hub-anything\AnythingProtocolFixture.exe >builds\hub-anything\fixture-build.log 2>&1 /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /O2 /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DNH_LEGACY_PROTOCOL /Iexternal\nlohmann tests\hub\AnythingProtocolFixture.cpp /Fobuilds\hub-anything\legacy-fixture.obj /Febuilds\hub-anything\LegacyProtocolFixture.exe >builds\hub-anything\legacy-build.log 2>&1 /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /O2 /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Iexternal\nlohmann tests\window-worker\SessionFixture.cpp /Fobuilds\hub-anything\source.obj /Febuilds\hub-anything\SessionFixture.exe /link user32.lib gdi32.lib >builds\hub-anything\source-build.log 2>&1
if errorlevel 1 exit /b %ERRORLEVEL%
if exist apps\NeuRoticHub\anything\AnythingController.cpp (
 cl /nologo /O2 /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Iexternal\nlohmann /Iapps\NeuRoticHub tests\hub\AnythingTests.cpp apps\NeuRoticHub\anything\AnythingController.cpp apps\NeuRoticHub\library\ManualLibrary.cpp /Fobuilds\hub-anything\ /Febuilds\hub-anything\AnythingTests.exe /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp /link shell32.lib user32.lib ole32.lib >builds\hub-anything\tests-build.log 2>&1
) else (
 cl /nologo /O2 /EHsc /std:c++20 /utf-8 /Iapps\NeuRoticHub tests\hub\AnythingTests.cpp /Fobuilds\hub-anything\tests.obj /Febuilds\hub-anything\AnythingTests.exe >builds\hub-anything\tests-build.log 2>&1 /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
)
if errorlevel 1 exit /b %ERRORLEVEL%
if exist apps\NeuRoticHub\anything\AnythingView.cpp (
 cl /nologo /c /O2 /EHsc /std:c++20 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Iexternal\nlohmann /Iapps\NeuRoticHub /Iapps\NeuRoticHub\library /IOptiScaler\include /IOptiScaler\include\imgui apps\NeuRoticHub\anything\AnythingView.cpp /Fobuilds\hub-anything\view.obj >builds\hub-anything\view-build.log 2>&1 /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
)
exit /b %ERRORLEVEL%
