@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "langOut=%~dp0..\builds\ui-release\language-packs"
if not exist "%langOut%" mkdir "%langOut%"
if exist "%langOut%\test.txt" del "%langOut%\test.txt"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I external\nlohmann tests\LanguagePackTests.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp /Fe:"%langOut%\language-packs.exe" /Fo:"%langOut%\\" >"%langOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%langOut%\language-packs.exe" >"%langOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%langOut%\test.txt"
popd
exit /b 0
:failed
type "%langOut%\build.txt"
if exist "%langOut%\test.txt" type "%langOut%\test.txt"
popd
exit /b 1
