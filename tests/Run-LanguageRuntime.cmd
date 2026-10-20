@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "langRuntimeOut=%~dp0..\builds\ui-release\language-runtime"
if not exist "%langRuntimeOut%" mkdir "%langRuntimeOut%"
if exist "%langRuntimeOut%\test.txt" del "%langRuntimeOut%\test.txt"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /DNOMINMAX /I OptiScaler /I OptiScaler\include /I external\nlohmann tests\LanguageRuntimeTests.cpp OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp /Fe:"%langRuntimeOut%\tests.exe" /Fo:"%langRuntimeOut%\\" >"%langRuntimeOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%langRuntimeOut%\tests.exe" >"%langRuntimeOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%langRuntimeOut%\test.txt"
popd
exit /b 0
:failed
type "%langRuntimeOut%\build.txt"
if exist "%langRuntimeOut%\test.txt" type "%langRuntimeOut%\test.txt"
popd
exit /b 1
