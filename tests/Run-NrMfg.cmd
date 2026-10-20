@echo off
setlocal
if "%~1"=="" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
if not exist "%~1" mkdir "%~1"
if not exist "%~1" exit /b 1
pushd "%~dp0.."
if not "%ERRORLEVEL%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_mfg_setup.cpp /Fe:"%~1\nr_mfg_setup.exe" /Fo:"%~1\nr_mfg_setup.obj"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%~1\nr_mfg_setup.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_mfg_resolution.cpp /Fe:"%~1\nr_mfg_resolution.exe" /Fo:"%~1\nr_mfg_resolution.obj"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%~1\nr_mfg_resolution.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX /I external\streamline tests\nr_mfg_control.cpp /Fe:"%~1\nr_mfg_control.exe" /Fo:"%~1\nr_mfg_control.obj"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%~1\nr_mfg_control.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_mfg_provider.cpp /Fe:"%~1\nr_mfg_provider.exe" /Fo:"%~1\nr_mfg_provider.obj"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%~1\nr_mfg_provider.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_mfg_temporal.cpp /Fe:"%~1\nr_mfg_temporal.exe" /Fo:"%~1\nr_mfg_temporal.obj"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%~1\nr_mfg_temporal.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_mfg_publication.cpp /Fe:"%~1\nr_mfg_publication.exe" /Fo:"%~1\nr_mfg_publication.obj"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%~1\nr_mfg_publication.exe"
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
