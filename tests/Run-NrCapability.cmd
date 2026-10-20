@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
pushd "%~dp0.."
set "capOut=builds\capability-facts\tests"
if not exist "%capOut%" mkdir "%capOut%"
set "capSrc=OptiScaler\nr\diagnostics\capability"
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_capability.cpp %capSrc%\CapabilityStore.cpp %capSrc%\CapabilityQuery.cpp %capSrc%\CapabilityStaging.cpp /Fe:"%capOut%\nr_capability.exe" /Fo:"%capOut%\\" >"%capOut%\compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%capOut%\compile.log" & popd & exit /b 1)
"%capOut%\nr_capability.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_capability_concurrency.cpp "%capOut%\CapabilityStore.obj" "%capOut%\CapabilityQuery.obj" "%capOut%\CapabilityStaging.obj" /Fe:"%capOut%\nr_capability_concurrency.exe" /Fo:"%capOut%\nr_capability_concurrency.obj" >"%capOut%\concurrency-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%capOut%\concurrency-compile.log" & popd & exit /b 1)
"%capOut%\nr_capability_concurrency.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_capability_registry.cpp "%capOut%\CapabilityStore.obj" "%capOut%\CapabilityQuery.obj" "%capOut%\CapabilityStaging.obj" /Fe:"%capOut%\nr_capability_registry.exe" /Fo:"%capOut%\nr_capability_registry.obj" >"%capOut%\registry-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%capOut%\registry-compile.log" & popd & exit /b 1)
"%capOut%\nr_capability_registry.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_capability_owners.cpp %capSrc%\CapabilityOwnerAdapters.cpp "%capOut%\CapabilityStore.obj" "%capOut%\CapabilityQuery.obj" "%capOut%\CapabilityStaging.obj" /Fe:"%capOut%\nr_capability_owners.exe" /Fo:"%capOut%\\" >"%capOut%\owners-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%capOut%\owners-compile.log" & popd & exit /b 1)
"%capOut%\nr_capability_owners.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_capability_ngx.cpp %capSrc%\CapabilityNgxObservation.cpp "%capOut%\CapabilityOwnerAdapters.obj" "%capOut%\CapabilityStore.obj" "%capOut%\CapabilityQuery.obj" "%capOut%\CapabilityStaging.obj" /Fe:"%capOut%\nr_capability_ngx.exe" /Fo:"%capOut%\\" >"%capOut%\ngx-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%capOut%\ngx-compile.log" & popd & exit /b 1)
"%capOut%\nr_capability_ngx.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_capability_export.cpp "%capOut%\CapabilityOwnerAdapters.obj" "%capOut%\CapabilityNgxObservation.obj" %capSrc%\CapabilityExport.cpp %capSrc%\CapabilityWireValidation.cpp "%capOut%\CapabilityStore.obj" "%capOut%\CapabilityQuery.obj" "%capOut%\CapabilityStaging.obj" /Fe:"%capOut%\nr_capability_export.exe" /Fo:"%capOut%\\" >"%capOut%\export-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%capOut%\export-compile.log" & popd & exit /b 1)
"%capOut%\nr_capability_export.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_capability_allocation.cpp "%capOut%\CapabilityStore.obj" /Fe:"%capOut%\nr_capability_allocation.exe" /Fo:"%capOut%\nr_capability_allocation.obj" >"%capOut%\allocation-compile.log" 2>&1
if not "%ERRORLEVEL%"=="0" (type "%capOut%\allocation-compile.log" & popd & exit /b 1)
"%capOut%\nr_capability_allocation.exe"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
call tests\Run-NrCapabilityRefresh.cmd
set "capExit=%ERRORLEVEL%"
popd
exit /b %capExit%
