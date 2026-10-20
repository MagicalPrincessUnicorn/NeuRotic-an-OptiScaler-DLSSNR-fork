$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work=Join-Path $repo 'builds/audit-model-deployment'
if(Test-Path -LiteralPath $work){throw 'Inspect previous owned model fixture before reuse'}
[void][IO.Directory]::CreateDirectory($work);$env:NEUROTIC_HUB_FIXTURE_ROOT=$work
. (Join-Path $repo 'tools/packaging/customer/support/NeuRotic-RuntimeFiles.ps1')
$engine=Join-Path $repo 'tools/packaging/customer/support/NeuRotic-Setup-Engine.ps1'
$ast=[Management.Automation.Language.Parser]::ParseFile($engine,[ref]$null,[ref]$null)
foreach($definition in $ast.EndBlock.Statements|Where-Object{$_ -is [Management.Automation.Language.FunctionDefinitionAst]}){. ([scriptblock]::Create($definition.Extent.Text))}
$production=Join-Path $repo 'builds/hub/bin'
$canonical=Join-Path (Split-Path $repo) 'provider-baselines/20260926-canonical/nvngx_dlssnr.dll'
$root=Get-NeuroticRuntimeRoot;[void][IO.Directory]::CreateDirectory($root)
[IO.File]::WriteAllText((Join-Path $root 'anything.json'),(@{schemaVersion=1;modelPath=$canonical}|ConvertTo-Json))
$components=Get-NeuroticRuntimeComponents $production
if($components.neuralModel.status -ne 'Present'){throw ('Production model verifier refused canonical fixture: '+$components.neuralModel.reason)}
$expected=(Get-FileHash -LiteralPath $canonical).Hash
if($components.neuralModel.sha256 -ne $expected){throw 'Verifier identity differs from preserved canonical model'}
Write-Output 'PASS Production canonical verifier supplies the exact deployment identity'
$game=Join-Path $work 'game';[void][IO.Directory]::CreateDirectory($game)
$projection=@(Get-NeuroticRuntimeProjection $production $game $false)
$model=@($projection|Where-Object{$_.path -eq 'nvngx_dlssnr.dll'})
if($model.Count -ne 1 -or $model[0].installed_hash -ne $expected){throw 'Validated model was not projected to the native consumer'}
$target=Join-Path $game $model[0].path
CopyVerified $model[0].source $target $model[0].installed_hash 'Absent'
if((HashFile $target) -ne $expected){throw 'Managed model copy differs'}
Write-Output 'PASS Production managed copy accepts the 165 MB canonical model and verifies destination bytes'
$prepared=@(Get-NeuroticRuntimeProjection $production $game $true)
if(@($prepared|Where-Object{$_.path -eq 'NeuRotic\Prepared\NeuRotic.GpuHost\nvngx_dlssnr.dll' -and $_.installed_hash -eq $expected}).Count -ne 1){throw 'Prepared route did not project the verified private model'}
Write-Output 'PASS Prepared model projection selects the GPU host consumer directory'
