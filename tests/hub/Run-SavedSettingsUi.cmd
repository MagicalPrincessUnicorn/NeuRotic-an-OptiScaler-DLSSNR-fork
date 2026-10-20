@echo off
setlocal
cd /d "%~dp0..\.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
if not exist builds\hub\saved-settings-ui mkdir builds\hub\saved-settings-ui
cl /nologo /std:c++20 /EHsc /utf-8 /W3 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I apps\NeuRoticHub /I apps\NeuRoticHub\library /I OptiScaler /I OptiScaler\include /I OptiScaler\include\imgui /I external\nlohmann /I external\freetype tests\hub\SavedSettingsUiTests.cpp tests\hub\KeybindUiTests.cpp tests\hub\NumericUiTests.cpp OptiScaler\menu\Localization.cpp /I external\nlohmann OptiScaler\menu\localization\LanguageRuntime.cpp OptiScaler\menu\localization\LanguagePack.cpp OptiScaler\menu\localization\LanguageCatalog.cpp OptiScaler\menu\localization\LanguageBundles.cpp OptiScaler\include\imgui\imgui.cpp OptiScaler\include\imgui\imgui_draw.cpp OptiScaler\include\imgui\imgui_widgets.cpp OptiScaler\include\imgui\imgui_tables.cpp OptiScaler\include\imgui\misc\freetype\imgui_freetype.cpp /Fe:builds\hub\saved-settings-ui\tests.exe /Fo:builds\hub\saved-settings-ui\ /link user32.lib comdlg32.lib bcrypt.lib external\freetype\freetype.lib
if errorlevel 1 exit /b 1
builds\hub\saved-settings-ui\tests.exe builds\hub\settings-mirror\schema.json
