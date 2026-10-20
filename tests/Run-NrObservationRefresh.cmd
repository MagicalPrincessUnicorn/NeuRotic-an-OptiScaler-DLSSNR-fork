@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
pushd "%~dp0.."
set "refreshOut=builds\observation-refresh\tests"
if not exist "%refreshOut%" mkdir "%refreshOut%"
if "%~1"=="worker" goto worker
if "%~1"=="control" goto control
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_candidate_manual.cpp /Fe:"%refreshOut%\manual.exe" /Fo:"%refreshOut%\manual.obj" >"%refreshOut%\manual-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%refreshOut%\manual-compile.log" & popd & exit /b 1)
"%refreshOut%\manual.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /DNR_CANDIDATE_MANUAL_TEST /I OptiScaler tests\nr_candidate_manual_worker.cpp OptiScaler\nr\diagnostics\candidate\Observer.cpp /Fe:"%refreshOut%\manual-worker.exe" /Fo:"%refreshOut%\\" >"%refreshOut%\manual-worker-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%refreshOut%\manual-worker-compile.log" & popd & exit /b 1)
"%refreshOut%\manual-worker.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
if "%~1"=="manual" (popd & exit /b 0)
:control
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_observation_refresh.cpp /Fe:"%refreshOut%\control.exe" /Fo:"%refreshOut%\control.obj" >"%refreshOut%\control-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%refreshOut%\control-compile.log" & popd & exit /b 1)
"%refreshOut%\control.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
set "refreshCap=OptiScaler\nr\diagnostics\capability"
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I OptiScaler tests\nr_observation_localization_stub.cpp OptiScaler\dlssnr\connections\ConnectionSelection.cpp tests\nr_observation_refresh_report.cpp %refreshCap%\ObservationRefreshReport.cpp %refreshCap%\CapabilityStore.cpp %refreshCap%\CapabilityExport.cpp %refreshCap%\CapabilityWireValidation.cpp /Fe:"%refreshOut%\report.exe" /Fo:"%refreshOut%\\" >"%refreshOut%\report-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%refreshOut%\report-compile.log" & popd & exit /b 1)
"%refreshOut%\report.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
:worker
set "refreshCap=OptiScaler\nr\diagnostics\capability"
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /DNR_OBSERVATION_REFRESH_TEST /I OptiScaler tests\nr_observation_localization_stub.cpp OptiScaler\dlssnr\connections\ConnectionSelection.cpp tests\nr_observation_refresh_worker.cpp %refreshCap%\ObservationRefresh.cpp %refreshCap%\ObservationRefreshReport.cpp %refreshCap%\CapabilityStore.cpp %refreshCap%\CapabilityExport.cpp %refreshCap%\CapabilityWireValidation.cpp OptiScaler\nr\diagnostics\candidate\Observer.cpp /Fe:"%refreshOut%\worker.exe" /Fo:"%refreshOut%\\" >"%refreshOut%\worker-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%refreshOut%\worker-compile.log" & popd & exit /b 1)
"%refreshOut%\worker.exe"
set "refreshExit=%ERRORLEVEL%"
popd
exit /b %refreshExit%
