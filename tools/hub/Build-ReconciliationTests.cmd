@echo off
setlocal
cd /d "%~dp0..\.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
if not exist builds\hub\reconciliation mkdir builds\hub\reconciliation
cl /nologo /EHsc /MD /std:c++20 /Iapps\NeuRoticHub /Iexternal\nlohmann tests\hub\FileConflictReviewTests.cpp /Fobuilds\hub\reconciliation\ /Febuilds\hub\reconciliation\FileConflictReviewTests.exe /I OptiScaler /I external\nlohmann OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp
if errorlevel 1 exit /b 1
builds\hub\reconciliation\FileConflictReviewTests.exe
exit /b %errorlevel%
