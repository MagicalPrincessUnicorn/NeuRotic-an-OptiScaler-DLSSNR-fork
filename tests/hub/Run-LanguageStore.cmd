@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "langStoreOut=%~dp0..\..\builds\hub\language-store"
if not exist "%langStoreOut%" mkdir "%langStoreOut%"
if exist "%langStoreOut%\test.txt" del "%langStoreOut%\test.txt"
pushd "%~dp0..\.."
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /DNOMINMAX /I apps\NeuRoticHub /I OptiScaler /I OptiScaler\include /I external\nlohmann tests\hub\LanguageStoreTests.cpp apps\NeuRoticHub\languages\LanguageStore.cpp OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp /I OptiScaler /I external\nlohmann        /Fe:"%langStoreOut%\tests.exe" /Fo:"%langStoreOut%\\" /link ole32.lib >"%langStoreOut%\build.txt" 2>&1
if errorlevel 1 goto failed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\hub\Prepare-LanguageStoreFixture.ps1
if errorlevel 1 goto failed
"%langStoreOut%\tests.exe" "%langStoreOut%\fixture" >"%langStoreOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%langStoreOut%\test.txt"
popd
exit /b 0
:failed
type "%langStoreOut%\build.txt"
if exist "%langStoreOut%\test.txt" type "%langStoreOut%\test.txt"
popd
exit /b 1
