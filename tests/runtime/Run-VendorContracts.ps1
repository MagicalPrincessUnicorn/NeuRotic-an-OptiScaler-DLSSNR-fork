param([string]$OutputDirectory='builds/core-runtime-audit/vendor-contracts')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out=Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force $out | Out-Null
function FunctionText([string]$source,[string]$signature){
 $start=$source.IndexOf($signature);if($start -lt 0){throw "Missing $signature"}
 $open=$source.IndexOf('{',$start);$depth=1;$end=$open+1
 while($depth -and $end -lt $source.Length){if($source[$end] -eq '{'){++$depth};if($source[$end] -eq '}'){--$depth};++$end}
 $source.Substring($start,$end-$start)
}
$schema=Get-Content -Raw (Join-Path $root 'OptiScaler/runtime/OwnedNgxCreateParameters.h')
$schemaBody=FunctionText $schema 'inline std::shared_ptr<NVSDK_NGX_Parameter> SnapshotCreateParameters'
$defines=([regex]::Matches($schemaBody,'NVSDK_NGX_Parameter_[A-Za-z0-9_]+') | ForEach-Object Value | Sort-Object -Unique | ForEach-Object {"#define $_ `"$_`""}) -join "`n"
$feature=Get-Content -Raw (Join-Path $root 'OptiScaler/upscalers/IFeature_Dx12.h')
$notifications=Get-Content -Raw (Join-Path $root 'OptiScaler/include/imgui/ImGuiNotify.hpp')
$storageStart=$notifications.IndexOf('    inline std::vector<ImGuiToast> notifications;')
$storageEnd=$notifications.IndexOf('/**',$storageStart)
$cpp=Get-Content -Raw (Join-Path $PSScriptRoot 'vendor_contract_tests.cpp.in')
$cpp=$cpp.Replace('// SCHEMA_DEFINES',$defines).Replace('// SCHEMA_FUNCTION',$schemaBody)
$cpp=$cpp.Replace('// RETIRE_FUNCTION',(FunctionText $feature 'bool CanRetire() const'))
$cpp=$cpp.Replace('// NOTIFICATION_STORAGE',$notifications.Substring($storageStart,$storageEnd-$storageStart))
$cpp=$cpp.Replace('// NOTIFICATION_INSERT',(FunctionText $notifications 'inline void InsertNotification('))
$releaseFunctions=@()
foreach($provider in @('dlss','dlssd')){
 $class=if($provider -eq 'dlss'){'DLSSFeatureDx12'}else{'DLSSDFeatureDx12'}
 $releaseSource=Get-Content -Raw (Join-Path $root "OptiScaler/upscalers/$provider/${class}.cpp".Replace('FeatureDx12','Feature_Dx12'))
 $releaseFunctions+=FunctionText $releaseSource "NVSDK_NGX_Result ${class}::ReleaseProvider()"
}
$cpp=$cpp.Replace('// RELEASE_FUNCTIONS',($releaseFunctions -join "`n"))
$feeder=Get-Content -Raw (Join-Path $root 'OptiScaler/inputs/FG/Streamline_Inputs_Dx12.cpp')
$cpp=$cpp.Replace('// PUBLISH_FUNCTION',(FunctionText $feeder 'const auto publish = [&]()')+';')
Set-Content (Join-Path $out 'vendor-contracts.cpp') $cpp
$cmd=@'
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /DNOMINMAX /W4 /WX /I "SOURCE_ROOT\OptiScaler" vendor-contracts.cpp /Fe:vendor-contracts.exe /Fo:vendor-contracts.obj
if errorlevel 1 exit /b 1
vendor-contracts.exe
'@
Set-Content (Join-Path $out 'run-vendor-contracts.cmd') $cmd.Replace('SOURCE_ROOT',$root)
Push-Location $out
try {& cmd.exe /c run-vendor-contracts.cmd;$result=$LASTEXITCODE} finally {Pop-Location;Remove-Item (Join-Path $out 'vendor-contracts.obj') -ErrorAction SilentlyContinue}
exit $result
