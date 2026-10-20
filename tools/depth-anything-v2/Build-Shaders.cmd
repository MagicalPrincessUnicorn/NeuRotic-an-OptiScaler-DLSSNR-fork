@echo off
setlocal
cd /d "%~dp0..\.."
if not exist builds\depth-anything-v2\shaders mkdir builds\depth-anything-v2\shaders
for %%S in (PrepareInput MaterializeDepth EstimateRange NormalizeRelative) do (
 OptiScaler\shaders\shader_tools\dxc.exe -T cs_6_0 -E CSMain -WX -O3 -Fh builds\depth-anything-v2\shaders\%%S.h -Vn Dav2%%S apps\NeuRoticWindowWorker\guidance\dav2\%%S.hlsl
 if errorlevel 1 exit /b 1
)
exit /b 0
