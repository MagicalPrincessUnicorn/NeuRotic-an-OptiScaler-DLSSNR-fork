param([string]$VisualStudio='C:\Program Files\Microsoft Visual Studio\18\Community',[string]$EvidenceDirectory='docs/implementation-evidence/app-refinement-20261004')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$work=[IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/model-tests'))
$evidence=Join-Path $repo $EvidenceDirectory
if(-not $work.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture path escaped checkout'}
function Assert-OrdinaryTree([string]$Path){
 if(-not (Test-Path -LiteralPath $Path)){return}
 $items=[Collections.Generic.Stack[string]]::new();$items.Push($Path)
 while($items.Count){$item=Get-Item -LiteralPath $items.Pop() -Force;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Preserve linked fixture: $($item.FullName)"};if($item.PSIsContainer){foreach($child in Get-ChildItem -LiteralPath $item.FullName -Force){$items.Push($child.FullName)}}}
}
Assert-OrdinaryTree $work
New-Item -ItemType Directory -Force (Join-Path $work 'support'),(Join-Path $work 'obj'),$evidence | Out-Null
# This test executable is isolated from the real installer. All helper children
# remain idle until the fixture feeds a receipt or explicitly stops them.
Set-Content -LiteralPath (Join-Path $work 'support/NeuRotic-Setup-Engine.ps1') -Value 'param([switch]$HubNonInteractive,[string]$HubRequestPath,[string]$HubResultPath) Start-Sleep -Seconds 30'
$vcvars=Join-Path $VisualStudio 'VC/Auxiliary/Build/vcvars64.bat'
if(-not (Test-Path -LiteralPath $vcvars)){throw 'Visual C++ toolchain unavailable'}
$command=@"
@echo off
call "$vcvars" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /MT /Od /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DNEUROTIC_OPERATION_FLOW_STANDALONE /Iexternal/nlohmann /Iapps/NeuRoticHub /Iapps/NeuRoticHub/library /Fobuilds/hub/model-tests/obj/ /Febuilds/hub/model-tests/OperationFlowTests.exe tests/hub/OperationFlowTests.cpp apps/NeuRoticHub/ui/HubViewModel.cpp apps/NeuRoticHub/install/OperationController.cpp apps/NeuRoticHub/library/ManualLibrary.cpp apps/NeuRoticHub/ui/ProfileCache.cpp apps/NeuRoticHub/ui/GameDiagnostics.cpp apps/NeuRoticHub/ui/DiagnosticBundle.cpp apps/NeuRoticHub/anything/AnythingController.cpp /link shell32.lib ole32.lib advapi32.lib shlwapi.lib user32.lib
exit /b %ERRORLEVEL%
"@
Set-Content -LiteralPath (Join-Path $work 'build.cmd') -Value $command
$priorFixture=$env:NEUROTIC_HUB_FIXTURE_ROOT
function IntermediateBytes {return [long]((Get-ChildItem -LiteralPath (Join-Path $work 'obj') -File | Measure-Object Length -Sum).Sum)}
$storage=[ordered]@{before=IntermediateBytes;built=0;after=0}
Push-Location $repo
try{
 & (Join-Path $work 'build.cmd') *> (Join-Path $evidence 'model-build-final.log')
 if($LASTEXITCODE){throw 'Focused model fixture compilation failed; see model-build-final.log'}
 $storage.built=IntermediateBytes
 $env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'user'
 & (Join-Path $work 'OperationFlowTests.exe') *> (Join-Path $evidence 'model-final.log')
 if($LASTEXITCODE){throw 'Focused model fixture failed; see model-final.log'}
 Get-Content -LiteralPath (Join-Path $evidence 'model-final.log')
}finally{
 $env:NEUROTIC_HUB_FIXTURE_ROOT=$priorFixture
 Pop-Location
 Assert-OrdinaryTree $work
 Get-ChildItem -LiteralPath (Join-Path $work 'obj') -File | Where-Object Extension -in '.obj','.pch','.ilk' | Remove-Item -Force
 $user=Join-Path $work 'user';if(Test-Path -LiteralPath $user){Remove-Item -LiteralPath $user -Recurse -Force}
 $storage.after=IntermediateBytes
 $storage | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'model-storage.json')
}
