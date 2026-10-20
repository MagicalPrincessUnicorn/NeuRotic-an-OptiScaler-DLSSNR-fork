@echo off
call "%~dp0build_adapter_tests.cmd" %*
exit /b %errorlevel%
