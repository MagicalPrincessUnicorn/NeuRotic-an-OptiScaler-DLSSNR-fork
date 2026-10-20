@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0..\.."
"python" tests\hub\Generate-RrPresetFields.py
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++20 /utf-8 /Ibuilds\hub\rr-optional tests\hub\RrPresetPersistenceTests.cpp /Fobuilds\hub\rr-optional\test.obj /Febuilds\hub\rr-optional\test.exe /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
if errorlevel 1 exit /b 1
builds\hub\rr-optional\test.exe
exit /b %ERRORLEVEL%
