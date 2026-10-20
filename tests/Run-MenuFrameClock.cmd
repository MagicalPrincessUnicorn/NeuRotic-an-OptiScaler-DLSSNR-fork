@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
pushd "%~dp0.."
set "testOut=builds\ui-release\sleek"
if not exist "%testOut%" mkdir "%testOut%"
rem Only the backend's module handle is needed from the product PCH.
>"%testOut%\pch.h" echo #pragma once
>>"%testOut%\pch.h" echo #include ^<windows.h^>
>>"%testOut%\pch.h" echo inline HMODULE dllModule = GetModuleHandleW(nullptr);
cl /nologo /std:c++20 /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DIMGUI_USER_CONFIG=\"HubImConfig.h\" /I "%testOut%" /I apps\NeuRoticHub /I OptiScaler /I OptiScaler\include /I OptiScaler\include\imgui tests\menu_frame_clock_win32_test.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui_impl_win32.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp /Fo"%testOut%\\" /Fe"%testOut%\menu-frame-clock-test.exe" /link user32.lib gdi32.lib dwmapi.lib
if errorlevel 1 goto failed
"%testOut%\menu-frame-clock-test.exe"
:failed
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
