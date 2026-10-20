@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
set "testOut=builds\native-mfg\tests"
if not exist "%testOut%" mkdir "%testOut%"
for %%t in (architecture capability_policy ptx_retarget provider_profile provider_state blackwell_temporal endpoint_backend) do (
  cl /nologo /std:c++20 /EHsc /W3 /DNR_EXPERIMENTAL_MFG_TEST /I external\mfg_ampere\contrib\validation\tests\provider_mocks external\mfg_ampere\contrib\validation\tests\%%t_test.cpp /Fe:"%testOut%\experimental_%%t.exe" /Fo:"%testOut%\experimental_%%t.obj"
  if errorlevel 1 goto failed
  "%testOut%\experimental_%%t.exe"
  if errorlevel 1 goto failed
)
popd
exit /b 0
:failed
popd
exit /b 1
