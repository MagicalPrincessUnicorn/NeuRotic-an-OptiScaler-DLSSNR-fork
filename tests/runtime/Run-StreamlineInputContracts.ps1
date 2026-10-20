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
$source=Get-Content -Raw (Join-Path $root 'OptiScaler/inputs/FG/Streamline_Inputs_Dx12.cpp')
$functions=@('uint64_t Sl_Inputs_Dx12::CaptureOwner()','void Sl_Inputs_Dx12::acceptedTags(', 'void Sl_Inputs_Dx12::CheckForFrame(', 'int Sl_Inputs_Dx12::IndexForFrameId(', 'void Sl_Inputs_Dx12::markPresent(') | ForEach-Object {FunctionText $source $_}
$cpp=(Get-Content -Raw (Join-Path $PSScriptRoot 'streamline_contract_tests.cpp.in')).Replace('// PRODUCTION_FUNCTIONS',($functions -join "`n"))
Set-Content (Join-Path $out 'streamline-contracts.cpp') $cpp
$cmd=@'
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I "SOURCE_ROOT\OptiScaler" streamline-contracts.cpp /Fe:streamline-contracts.exe /Fo:streamline-contracts.obj
if errorlevel 1 exit /b 1
streamline-contracts.exe
'@
Set-Content (Join-Path $out 'run-streamline-contracts.cmd') $cmd.Replace('SOURCE_ROOT',$root)
Push-Location $out
try {& cmd.exe /c run-streamline-contracts.cmd;$result=$LASTEXITCODE} finally {Pop-Location;Remove-Item (Join-Path $out 'streamline-contracts.obj') -ErrorAction SilentlyContinue}
exit $result
