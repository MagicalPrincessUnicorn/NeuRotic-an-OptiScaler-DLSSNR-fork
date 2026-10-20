@echo off
setlocal
set "OUT=%~dp0..\..\builds\universal-connections\helper"
"%OUT%\helper_gpu_protocol64.exe"
if errorlevel 1 exit /b 1
"%OUT%\helper_gpu_protocol32.exe"
if errorlevel 1 exit /b 1
"%OUT%\helper_gpu_host64.exe" "%OUT%\helper_gpu_host64.exe"
if errorlevel 1 exit /b 1
"%OUT%\helper_gpu_client32.exe" "%OUT%\helper_gpu_host64.exe"
if errorlevel 1 exit /b 1
"%OUT%\helper_gpu_client32.exe" "%OUT%\helper_gpu_host64.exe" --host-created
if errorlevel 1 exit /b 1
"%OUT%\helper_gpu_client32.exe" "%OUT%\helper_gpu_host64.exe" --bridge
if errorlevel 1 exit /b 1
"%OUT%\helper_gpu_client32.exe" "%OUT%\helper_gpu_host64.exe" --preparing
if errorlevel 1 exit /b 1
"%OUT%\helper_gpu_client32.exe" "%OUT%\helper_gpu_host64.exe" --pending
if errorlevel 1 exit /b 1
"%OUT%\helper_gpu_client32.exe" "%OUT%\helper_gpu_host64.exe" --seal-refused
exit /b %errorlevel%
