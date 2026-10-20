@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /std:c++20 /EHsc /I OptiScaler /I apps/NeuRoticHub /I external/nlohmann tests/universal_connections/selection_status_window_tests.cpp OptiScaler/dlssnr/connections/ConnectionSelection.cpp OptiScaler/dlssnr/connections/ConnectionPolicy.cpp OptiScaler/dlssnr/PreparedGuideStatusV2.cpp /Fobuilds/universal-connections/policy/ /Febuilds/universal-connections/policy/tests.exe
if errorlevel 1 exit /b %errorlevel%
builds\universal-connections\policy\tests.exe
