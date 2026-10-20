@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not exist builds\inspector-motion\object-rules\ui mkdir builds\inspector-motion\object-rules\ui
cl /nologo /std:c++20 /EHsc /utf-8 /W3 /I OptiScaler /I OptiScaler\include /I external\nlohmann /I external\freetype tests\nr\semantic\object_rules_imgui_tests.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:builds\inspector-motion\object-rules\ui\ui.exe /Fo:builds\inspector-motion\object-rules\ui\ /link user32.lib external\freetype\freetype.lib
if errorlevel 1 exit /b 1
builds\inspector-motion\object-rules\ui\ui.exe
