@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
"python" -B "%~dp0nr_bridge_mutations.py"
exit /b %ERRORLEVEL%
