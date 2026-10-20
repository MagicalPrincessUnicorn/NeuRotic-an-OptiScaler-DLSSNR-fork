param([string]$VisualStudio='C:\Program Files\Microsoft Visual Studio\18\Community',[string]$EvidenceDirectory='docs/implementation-evidence/desktop-game-model-flow-20261005',[string]$Label='grouping')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$work=[IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/grouping-tests'))
if(-not $work.StartsWith($repo+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture escaped checkout'}
function Assert-OrdinaryTree([string]$Path){if(-not(Test-Path -LiteralPath $Path)){return};$items=[Collections.Generic.Stack[string]]::new();$items.Push($Path);while($items.Count){$item=Get-Item -LiteralPath $items.Pop() -Force;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture refused'};if($item.PSIsContainer){foreach($child in Get-ChildItem -LiteralPath $item.FullName -Force){$items.Push($child.FullName)}}}}
Assert-OrdinaryTree $work
$evidence=Join-Path $repo $EvidenceDirectory
New-Item -ItemType Directory -Force (Join-Path $work 'support'),(Join-Path $work 'obj'),$evidence|Out-Null
Set-Content -LiteralPath (Join-Path $work 'support/NeuRotic-Setup-Engine.ps1') -Value 'param([switch]$HubNonInteractive,[string]$HubRequestPath,[string]$HubResultPath) Start-Sleep -Seconds 30'
$vcvars=Join-Path $VisualStudio 'VC/Auxiliary/Build/vcvars64.bat'
$command=@"
@echo off
call "$vcvars" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /MT /Od /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Iexternal/nlohmann /Iapps/NeuRoticHub /Iapps/NeuRoticHub/library /Fobuilds/hub/grouping-tests/obj/ /Febuilds/hub/grouping-tests/GameGroupingTests.exe tests/hub/GameGroupingTests.cpp apps/NeuRoticHub/ui/HubViewModel.cpp apps/NeuRoticHub/install/OperationController.cpp apps/NeuRoticHub/library/ManualLibrary.cpp apps/NeuRoticHub/ui/ProfileCache.cpp apps/NeuRoticHub/ui/GameDiagnostics.cpp apps/NeuRoticHub/ui/DiagnosticBundle.cpp apps/NeuRoticHub/anything/AnythingController.cpp /link shell32.lib ole32.lib advapi32.lib shlwapi.lib user32.lib
exit /b %ERRORLEVEL%
"@
Set-Content -LiteralPath (Join-Path $work 'build.cmd') -Value $command
$prior=$env:NEUROTIC_HUB_FIXTURE_ROOT
function Bytes{return [long]((Get-ChildItem -LiteralPath (Join-Path $work 'obj') -File|Measure-Object Length -Sum).Sum)}
$storage=[ordered]@{before=Bytes;built=0;after=0}
Push-Location $repo
try{
 & (Join-Path $work 'build.cmd') *> (Join-Path $evidence "$Label-build.log")
 if($LASTEXITCODE){throw 'Grouping fixture compilation failed'}
 $storage.built=Bytes;$env:NEUROTIC_HUB_FIXTURE_ROOT=Join-Path $work 'user'
 & (Join-Path $work 'GameGroupingTests.exe') *> (Join-Path $evidence "$Label-tests.log")
 Get-Content -LiteralPath (Join-Path $evidence "$Label-tests.log")
 if($LASTEXITCODE){throw 'Grouping regressions failed'}
}finally{
 $env:NEUROTIC_HUB_FIXTURE_ROOT=$prior;Pop-Location;Assert-OrdinaryTree $work
 Get-ChildItem -LiteralPath (Join-Path $work 'obj') -File|Where-Object Extension -in '.obj','.pch','.ilk'|Remove-Item -Force
 $user=Join-Path $work 'user';if(Test-Path -LiteralPath $user){Remove-Item -LiteralPath $user -Recurse -Force}
 $storage.after=Bytes;$storage|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence "$Label-storage.json")
}
