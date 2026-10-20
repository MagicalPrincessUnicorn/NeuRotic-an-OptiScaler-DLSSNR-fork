param([string]$OutputDirectory='builds/core-runtime-audit/vendor-contracts')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out=Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force $out | Out-Null
$source=Get-Content -Raw (Join-Path $root 'OptiScaler/upscalers/IFeature_Dx12.cpp')
$begin=$source.IndexOf('    if (!evalResult)')
$end=$source.IndexOf('    // imgui',$begin)
$fragment=$source.Substring($begin,$end-$begin)
$cpp=@'
#include <cstdio>
#include <vector>
#include <stdexcept>
struct ID3D12Resource{};
constexpr int NVSDK_NGX_Parameter_Output=1;
struct NVSDK_NGX_Parameter{ID3D12Resource* output;void Set(int,ID3D12Resource* value){output=value;}};
struct Pass {void* inputBuffer=(void*)1;void* outputBuffer=(void*)2;bool succeeds=false;bool Dispatch(void*,void*){return succeeds;}};
bool Run(bool evalResult,std::vector<Pass> pipeline){
'@
$cpp += $fragment + "return true;}`n"
$guardStart=$source.IndexOf('    struct OutputRestore')
$guardEnd=$source.IndexOf('    InParameters->Get(NVSDK_NGX_Parameter_MotionVectors',$guardStart)
if($guardStart -lt 0 -or $guardEnd -lt 0){throw 'Missing production Output restoration guard'}
$cpp += "bool Guarded(NVSDK_NGX_Parameter* InParameters,ID3D12Resource* paramOutput,bool throws){`n"
$cpp += $source.Substring($guardStart,$guardEnd-$guardStart)
$cpp += 'InParameters->Set(NVSDK_NGX_Parameter_Output,nullptr);if(throws)throw std::runtime_error("vendor");return false;}' + "`n"
$cpp += @'
int main(){int failures=0; if(Run(false,{}))++failures; if(Run(true,{{}}))++failures; if(!Run(true,{{(void*)1,(void*)2,true}}))++failures;
 ID3D12Resource original;NVSDK_NGX_Parameter parameters{&original};Guarded(&parameters,&original,false);if(parameters.output!=&original)++failures;
 try{Guarded(&parameters,&original,true);}catch(const std::runtime_error&){}if(parameters.output!=&original)++failures;
 std::printf("Output postpass/restoration: 5 checks, %d failures\n",failures);return failures?1:0;}
'@
Set-Content (Join-Path $out 'output.cpp') $cpp
$cmd=@'
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX output.cpp /Fe:output.exe /Fo:output.obj
if errorlevel 1 exit /b 1
output.exe
'@
Set-Content (Join-Path $out 'run.cmd') $cmd
Push-Location $out
try { & cmd.exe /c run.cmd; $result=$LASTEXITCODE } finally { Pop-Location; Remove-Item (Join-Path $out 'output.obj') -ErrorAction SilentlyContinue }
exit $result
