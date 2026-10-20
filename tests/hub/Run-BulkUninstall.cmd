@echo off
set "NR_BULK_PATH=%PATH%"
set PATH=
set "Path=%NR_BULK_PATH%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\hub\bulk-tests\obj mkdir builds\hub\bulk-tests\obj
cl /nologo /std:c++20 /EHsc /O2 /W4 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /Iexternal/nlohmann /Iapps/NeuRoticHub /Iapps/NeuRoticHub/library /Fobuilds/hub/bulk-tests/obj/ /Febuilds/hub/bulk-tests/BulkUninstallTests.exe tests/hub/BulkUninstallTests.cpp apps/NeuRoticHub/ui/ProfileCache.cpp apps/NeuRoticHub/library/ManualLibrary.cpp /link shell32.lib ole32.lib advapi32.lib user32.lib
if errorlevel 1 exit /b %ERRORLEVEL%
builds\hub\bulk-tests\BulkUninstallTests.exe
exit /b %ERRORLEVEL%
