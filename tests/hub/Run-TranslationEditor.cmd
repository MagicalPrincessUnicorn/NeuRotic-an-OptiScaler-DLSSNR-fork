@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "editorOut=%~dp0..\..\builds\hub\translation-editor"
if not exist "%editorOut%" mkdir "%editorOut%"
if exist "%editorOut%\test.txt" del "%editorOut%\test.txt"
pushd "%~dp0..\.."
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /W3 /DNOMINMAX /I apps\NeuRoticHub /I OptiScaler /I OptiScaler\include /I external\nlohmann tests\hub\TranslationEditorTests.cpp apps\NeuRoticHub\languages\TranslationEditorModel.cpp OptiScaler\menu\Localization.cpp OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp /I OptiScaler /I external\nlohmann        /Fe:"%editorOut%\tests.exe" /Fo:"%editorOut%\\" >"%editorOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%editorOut%\tests.exe" >"%editorOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%editorOut%\test.txt"
popd
exit /b 0
:failed
type "%editorOut%\build.txt"
if exist "%editorOut%\test.txt" type "%editorOut%\test.txt"
popd
exit /b 1
