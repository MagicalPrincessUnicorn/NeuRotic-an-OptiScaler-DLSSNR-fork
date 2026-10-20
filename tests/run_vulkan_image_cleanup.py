from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
out=root/'builds/vulkan-experiment/tests/image-cleanup'
out.mkdir(parents=True,exist_ok=True)
cmd='''@echo off
call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul
cl /nologo /std:c++20 /EHsc /O2 /I"{root}/external/vulkan/include" /I"{root}/external/nvngx_dlss_sdk" "{root}/tests/vulkan_image_cleanup.cpp" "{root}/OptiScaler/dlssnr/VulkanNrImageFacts.cpp" /Fe:image-cleanup.exe /Fo:.\\ /link /INCREMENTAL:NO
if errorlevel 1 exit /b %errorlevel%
image-cleanup.exe
exit /b %errorlevel%
'''.format(root=root.as_posix())
(out/'run.cmd').write_text(cmd,encoding='utf-8')
p=subprocess.run(['cmd','/d','/c','run.cmd'],cwd=out,capture_output=True,text=True)
print(p.stdout+p.stderr);sys.exit(p.returncode)
