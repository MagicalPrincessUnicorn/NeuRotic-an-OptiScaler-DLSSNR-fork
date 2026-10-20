# Shared read-only validation. Importing this helper or requesting a plan writes nothing.
function Assert-NeuRoticOwnedOutput {
 param([string]$Repository,[string]$Path,[switch]$Tree)
 $repo=[IO.Path]::GetFullPath($Repository).TrimEnd('\','/')
 $full=[IO.Path]::GetFullPath($Path)
 $owned=(Join-Path $repo 'builds').TrimEnd('\','/')+'\'
 if(-not $full.StartsWith($owned,[StringComparison]::OrdinalIgnoreCase)){throw ('Output not checkout owned: '+$Path)}
 $cursor=$full
 while($cursor){
  if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw ('Linked output refused: '+$cursor)}}
  $parent=Split-Path -Parent $cursor;if($parent -eq $cursor){break};$cursor=$parent
 }
 if($Tree -and (Test-Path -LiteralPath $full -PathType Container)){
  $queue=New-Object 'Collections.Generic.Queue[string]';$queue.Enqueue($full)
  while($queue.Count){foreach($entry in Get-ChildItem -LiteralPath $queue.Dequeue() -Force){
   if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw ('Linked output descendant refused: '+$entry.FullName)}
   if($entry.PSIsContainer){$queue.Enqueue($entry.FullName)}
  }}
 }
}
function Get-NeuRoticBuildContext {
 param([string]$RepositoryRoot,[string]$OutputRoot,[string]$Target,[string]$SharedExternalRoot,[string]$SourceManifest,[string]$BuildLabel)
 $repo=[IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\','/')
 if(-not(Test-Path -LiteralPath (Join-Path $repo 'OptiScaler.sln') -PathType Leaf)){throw 'Select an existing NeuRotic checkout'}
 if(-not $OutputRoot){$OutputRoot=Join-Path $repo ('builds/source/'+$Target)}
 $output=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\','/')
 Assert-NeuRoticOwnedOutput $repo $output -Tree
 $owned=[IO.Path]::GetFullPath((Join-Path $repo 'builds')).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
 if(-not $output.StartsWith($owned,[StringComparison]::OrdinalIgnoreCase)){throw 'OutputRoot must be inside this checkout builds directory'}
 $cursor=$output
 while($cursor -and $cursor.Length -ge $repo.Length){
  if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked output ancestry is not owned'}}
  $cursor=Split-Path -Parent $cursor
 }
 if(-not $SharedExternalRoot){$SharedExternalRoot=Join-Path $repo 'external'}
 $shared=[IO.Path]::GetFullPath($SharedExternalRoot);$dependencies=[ordered]@{}
 foreach($name in @('FidelityFX-SDK','FidelityFX-SDK-v2','magic_enum','nvapi','simpleini','spdlog','unordered_dense','vulkan','xess')){$dependencies[$name]=Join-Path $shared $name}
 if($BuildLabel -and $BuildLabel -notmatch '^[A-Za-z0-9._-]{1,80}$'){throw 'Invalid build label'}
 $manifestHash=$null;$inputs=@();$moduleCapture=$null
 if($SourceManifest){
  $manifestHash=(Get-FileHash -LiteralPath $SourceManifest -Algorithm SHA256).Hash.ToLowerInvariant()
  $document=Get-Content -Raw -LiteralPath $SourceManifest|ConvertFrom-Json
  if($Target -eq 'worker'){$moduleCapture=$document.module_capture}
  if($document.schema_version -ne 1 -or $document.target -ne $Target -or [IO.Path]::GetFullPath($document.repository).TrimEnd('\','/') -ine $repo){throw 'Source manifest context differs'}
  $inputs=@($document.files);if(-not $inputs.Count){throw 'Source manifest is empty'}
  $seen=@{}
  foreach($row in $inputs){
   if(-not [IO.Path]::IsPathRooted($row.path) -or $row.sha256 -notmatch '^[0-9a-f]{64}$' -or $seen.ContainsKey($row.path)){throw 'Invalid/duplicate source manifest row'}
   $seen[$row.path]=$true
   if((Get-Item -LiteralPath $row.path).Length -ne $row.bytes -or (Get-FileHash -LiteralPath $row.path).Hash -ine $row.sha256){throw ('Frozen input changed: '+$row.path)}
  }
  $contentLabel='099-dev-'+$manifestHash.Substring(0,16)
  if($BuildLabel -and $BuildLabel -ne $contentLabel){throw 'BuildLabel must identify the frozen manifest content'}
  $BuildLabel=$contentLabel
 }
 return [ordered]@{repository=$repo;output=$output;target=$Target;sharedDependencies=$dependencies;sourceManifest=$SourceManifest;sourceManifestSha256=$manifestHash;inputs=$inputs;buildLabel=$BuildLabel;moduleCapture=$moduleCapture;moduleCaptureComplete=$false;qualification='INTERNAL_DEVELOPMENT_PLAN_ONLY'}
}
function Assert-NeuRoticBuildInputs {
 param($Context,[string[]]$RequiredFiles,[string]$VCToolsVersion,[string]$WindowsSdkVersion)
 if(-not $Context.sourceManifest){throw 'A frozen source/dependency/tool manifest is required; HEAD alone does not identify dirty inputs'}
 $seen=@{};foreach($row in $Context.inputs){$seen[[IO.Path]::GetFullPath($row.path)]=$row}
 $python=@($Context.inputs|Where-Object{[IO.Path]::GetFileName($_.path) -ieq 'python.exe'})
 if($python.Count -ne 1){throw 'Exactly one actual Python evidence-host tool must be frozen'}
 $RequiredFiles+=@((Join-Path $PSScriptRoot 'BuildEvidence.py'),(Join-Path $PSScriptRoot 'ProducerCapture.py'),(Join-Path $Context.repository 'tools/packaging/customer/OfflineDependencies.py'),$python[0].path)
 $Context.python=$python[0].path
 foreach($path in $RequiredFiles){if(-not $seen.ContainsKey([IO.Path]::GetFullPath($path))){throw ('Required recipe/tool/source absent from freeze: '+$path)}}
 if(-not $env:VCToolsVersion -or -not $env:WindowsSDKVersion -or $env:VCToolsVersion.TrimEnd('\','/') -ne $VCToolsVersion -or $env:WindowsSDKVersion.TrimEnd('\','/') -ne $WindowsSdkVersion){throw 'Use the matching x64 compiler and SDK developer environment'}
 $env:CL=$null;$env:_CL_=$null;$env:MSBUILDDISABLENODEREUSE='1';$env:VSLANG='1033'
}
function Confirm-NeuRoticBuildInputs {
 param($Context)
 if((Get-FileHash -LiteralPath $Context.sourceManifest).Hash -ine $Context.sourceManifestSha256){throw 'Frozen manifest changed during build'}
 foreach($row in $Context.inputs){if((Get-Item -LiteralPath $row.path).Length -ne $row.bytes -or (Get-FileHash -LiteralPath $row.path).Hash -ine $row.sha256){throw ('Frozen input changed: '+$row.path)}}
}
function Initialize-NeuRoticObservedBuild {
 param($Context,[string]$MSBuild,[string[]]$Arguments,[bool]$NativeOnly=$true)
 Assert-NeuRoticOwnedOutput $Context.repository $Context.output -Tree
 # Tracking is captured only for this invocation. Existing objects can remain,
 # but a target skipped without a matching producer cannot earn a new receipt.
 $document=Get-Content -LiteralPath $Context.sourceManifest -Raw|ConvertFrom-Json
 if($Context.target -eq 'core'){throw 'Complete CMake/shader and native producer capture is not yet available; no build/receipt authorization inferred'}
 if($Context.target -eq 'app' -and -not $NativeOnly){throw 'Discovery requires its own observed offline .NET producer; native receipt cannot certify it'}
 if($Context.target -eq 'app' -and $document.native_only -ne $true){throw 'Freeze must explicitly select native_only=true'}
 if($Context.target -eq 'worker'){
  $module=$Context.moduleCapture
  if(-not $module -or -not $module.observer -or -not $module.offering -or -not $module.events -or $module.timeout_ms -le 0 -or $module.timeout_ms -gt [int]::MaxValue){throw 'Owner-frozen worker module capture context required'}
  $frozen=@{};foreach($row in $Context.inputs){$frozen[[IO.Path]::GetFullPath($row.path)]=$row}
  foreach($path in @($module.observer,$module.offering)){
   if(-not [IO.Path]::IsPathRooted($path) -or -not $frozen.ContainsKey([IO.Path]::GetFullPath($path))){throw ('Module observer/offering absent from freeze: '+$path)}
   $row=$frozen[[IO.Path]::GetFullPath($path)]
   if((Get-Item -LiteralPath $path).Length -ne $row.bytes -or (Get-FileHash -LiteralPath $path).Hash -ine $row.sha256){throw ('Frozen module observer/offering changed: '+$path)}
  }
  $offering=Get-Content -LiteralPath $module.offering -Raw|ConvertFrom-Json
  if($offering.schema_version -ne 1 -or $offering.target -ne 'worker' -or $offering.configuration -ne 'Release|x64' -or [IO.Path]::GetFullPath($offering.repository).TrimEnd('\','/') -ine $Context.repository -or [IO.Path]::GetFullPath($offering.output).TrimEnd('\','/') -ine $Context.output){throw 'Worker module offering context differs'}
  $actual=@($MSBuild)+@($Arguments)
  if(($actual|ConvertTo-Json -Compress) -cne (@($offering.argv)|ConvertTo-Json -Compress)){throw 'Worker module observed invocation differs'}
  $owned=$Context.output.TrimEnd('\','/')+'\'
  foreach($path in ((@($module.events)+@($module.retirements))|Where-Object{$_})){
   Assert-NeuRoticOwnedOutput $Context.repository $path
   if(-not [IO.Path]::GetFullPath($path).StartsWith($owned,[StringComparison]::OrdinalIgnoreCase)){throw ('Worker module output context differs: '+$path)}
  }
  $Context.moduleArgv=$actual;$Context.moduleCaptureComplete=$false
 }
 New-Item -ItemType Directory -Force -Path $Context.output|Out-Null
 $graphPath=Join-Path $Context.output 'evaluated-inputs.json';$expanded=Join-Path $Context.output 'evaluated-project.xml'
 foreach($p in @($graphPath,$expanded,(Join-Path $Context.output 'evaluation.log'))){Assert-NeuRoticOwnedOutput $Context.repository $p}
 $evalArgs=@($Arguments|Where-Object{$_ -notmatch '^/(t:|v:|m:)'} )
 $json=& $MSBuild @evalArgs '/getItem:ClCompile,ResourceCompile,FxCompile,CustomBuild' '/getProperty:MSBuildProjectFullPath' 2>&1|Out-String
 if($LASTEXITCODE -ne 0){throw ('Actual project evaluation failed: '+$json)}
 $evaluated=$json|ConvertFrom-Json
 & $MSBuild @evalArgs ('/pp:'+$expanded) 2>&1|Out-File -LiteralPath (Join-Path $Context.output 'evaluation.log')
 if($LASTEXITCODE -ne 0){throw 'Actual imported project evaluation failed'}
 $required=@();$seen=@{}
 foreach($kind in @('ClCompile','ResourceCompile','FxCompile','CustomBuild')){
  foreach($item in $evaluated.Items.$kind){
   if($item.ExcludedFromBuild -eq 'true'){continue}
   $path=$item.FullPath;if(-not $path){throw 'Evaluated input lacks absolute path'}
   if($seen.ContainsKey($path)){continue};$seen[$path]=$true
   $typed=@{ClCompile='source';ResourceCompile='resource';FxCompile='shader';CustomBuild='custom'}[$kind]
   $required+=@{path=$path;kind=$typed;bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}
  }
 }
 $imports=[regex]::Matches([IO.File]::ReadAllText($expanded),'(?m)^\s*([A-Za-z]:[\\/][^\r\n<>]+\.(?:targets|props|vcxproj))\s*$')
 if(-not $imports.Count){throw 'Actual evaluated import closure unavailable'}
 foreach($match in $imports){$path=$match.Groups[1].Value.Trim();if(-not $seen.ContainsKey($path)){$seen[$path]=$true;$required+=@{path=$path;kind='import';bytes=(Get-Item -LiteralPath $path).Length;sha256=(Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()}}}
 $known=@{};foreach($row in $Context.inputs){$known[[IO.Path]::GetFullPath($row.path)]=$row}
 foreach($row in $required){$path=[IO.Path]::GetFullPath($row.path);if(-not $known.ContainsKey($path)){throw ($row.kind+' input absent: '+$row.path)};if($row.sha256 -ne $known[$path].sha256){throw ('Evaluated input identity differs: '+$path)}}
 $evaluation=[ordered]@{configuration='Release|x64';inputs=$required}
 if($Context.target -eq 'worker'){$evaluation.argv=$Context.moduleArgv}
 $evaluation|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $graphPath -Encoding utf8
 foreach($tree in @('obj','forwarder-obj')){
  $path=Join-Path $Context.output $tree;Assert-NeuRoticOwnedOutput $Context.repository $path -Tree
  if(Test-Path -LiteralPath $path){foreach($track in Get-ChildItem -LiteralPath $path -File -Recurse -Filter '*.tlog'){Assert-NeuRoticOwnedOutput $Context.repository $track.FullName;Remove-Item -LiteralPath $track.FullName}}
 }
 $Context.evaluation=$graphPath
 $script:NeuRoticActiveBuildContext=$Context
}
function Invoke-NeuRoticBuildCommand {
 param([string]$Tool,[string[]]$Arguments,[string]$Log)
 # Build entry points preflight their complete owned tree before each tool.
 $repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
 Assert-NeuRoticOwnedOutput $repo $Log
 if($script:NeuRoticActiveBuildContext){Assert-NeuRoticOwnedOutput $script:NeuRoticActiveBuildContext.repository $script:NeuRoticActiveBuildContext.output -Tree;Confirm-NeuRoticBuildInputs $script:NeuRoticActiveBuildContext}
 if($script:NeuRoticActiveBuildContext -and $script:NeuRoticActiveBuildContext.target -eq 'worker'){
  $context=$script:NeuRoticActiveBuildContext;$module=$context.moduleCapture
  if(((@($Tool)+@($Arguments))|ConvertTo-Json -Compress) -cne (@($context.moduleArgv)|ConvertTo-Json -Compress)){throw 'Actual worker command differs from frozen module offering'}
  foreach($path in ((@($module.events)+@($module.retirements))|Where-Object{$_})){
   Assert-NeuRoticOwnedOutput $context.repository $path
   if(Test-Path -LiteralPath $path){throw ('Fresh worker module output required; refusing reuse: '+$path)}
  }
  & $module.observer $module.events $module.timeout_ms $Tool @Arguments 2>&1|Tee-Object -FilePath $Log -Append|Out-Host
  if($LASTEXITCODE -ne 0){throw ('Observed worker build failed; see '+$Log)}
  if(-not (Test-Path -LiteralPath $module.events -PathType Leaf)){throw 'Closed worker module stream absent'}
  $context.moduleCaptureComplete=$true
 }else{& $Tool @Arguments 2>&1|Tee-Object -FilePath $Log -Append|Out-Host}
 if($LASTEXITCODE -ne 0){throw ('Build command failed; see '+$Log)}
 if($script:NeuRoticActiveBuildContext){Confirm-NeuRoticBuildInputs $script:NeuRoticActiveBuildContext;Assert-NeuRoticOwnedOutput $script:NeuRoticActiveBuildContext.repository $script:NeuRoticActiveBuildContext.output -Tree}
}
function Write-NeuRoticBuildReceipt {
 param($Context,[string]$Log,[string]$GeneratedRoot)
 Confirm-NeuRoticBuildInputs $Context
 Assert-NeuRoticOwnedOutput $Context.repository $Context.output -Tree
 if(-not $Context.evaluation){throw 'No evaluated/observed producer closure; receipt refused'}
 $python=$Context.python;if(-not $python){throw 'Actual evidence-host tool not frozen'}
 $capture=Join-Path $PSScriptRoot 'BuildEvidence.py';$evidencePath=Join-Path $Context.output 'producer-evidence.json'
 Assert-NeuRoticOwnedOutput $Context.repository $evidencePath
 $captureArgs=@('--repo',$Context.repository,'--output',$Context.output,'--manifest',$Context.sourceManifest,'--log',$Log,'--evaluation',$Context.evaluation,'--result',$evidencePath)
 if($Context.target -eq 'worker'){
  if(-not $Context.moduleCaptureComplete -or -not $Context.producerSession -or -not $Context.producerSessionSha256){throw 'Completed worker module/session proof required before receipt'}
  $captureArgs+=@('--producer-session',$Context.producerSession,'--producer-session-sha256',$Context.producerSessionSha256,'--module-offering',$Context.moduleCapture.offering,'--module-events',$Context.moduleCapture.events)
  foreach($path in (@($Context.moduleCapture.retirements)|Where-Object{$_})){if(Test-Path -LiteralPath $path -PathType Leaf){$captureArgs+=@('--module-retirement',$path)}}
 }
 & $python -B $capture @captureArgs
 if($LASTEXITCODE -ne 0){throw 'Observed input/output producer closure refused; no build receipt'}
 $evidence=Get-Content -LiteralPath $evidencePath -Raw|ConvertFrom-Json
 $files=@();foreach($file in $evidence.selected_outputs){$files+=@{path=$file.path.Substring($Context.repository.Length+1).Replace('\','/');bytes=$file.bytes;sha256=$file.sha256}}
 Confirm-NeuRoticBuildInputs $Context
 $env:GIT_OPTIONAL_LOCKS='0'
 $sourceCommit=(& git -c ('safe.directory='+$Context.repository) -C $Context.repository rev-parse HEAD);if($LASTEXITCODE -ne 0){throw 'Source identity unavailable'}
 $trees=[ordered]@{}
 foreach($scope in @('apps/NeuRoticHub','apps/NeuRoticWindowWorker','OptiScaler','tools/window-worker','addons','localization')){
  $trees[$scope]=(& git -c ('safe.directory='+$Context.repository) -C $Context.repository rev-parse ($sourceCommit+':'+$scope));if($LASTEXITCODE -ne 0){throw 'Source scope identity unavailable'}
 }
 $receipt=[ordered]@{schema_version=2;target=$Context.target;qualification='INTERNAL_DEVELOPMENT_BUILD_ONLY';exit_code=0;source_unchanged=$true;repository=$Context.repository;source_commit=$sourceCommit;source_trees=$trees;build_label=$Context.buildLabel;source_manifest_path=$Context.sourceManifest;source_manifest_sha256=$Context.sourceManifestSha256;input_pins=$Context.inputs;evidence=$evidence;files=$files;log_sha256=(Get-FileHash -LiteralPath $Log).Hash.ToLowerInvariant();game_qualified=$false;publication_authorized=$false}
 if($Context.target -eq 'core'){
  $product=@($files|Where-Object{[IO.Path]::GetFileName($_.path) -eq 'OptiScaler.dll'});if($product.Count -ne 1){throw 'Expected exactly one native product output'}
  $receipt.product=$product[0];$receipt.product_tree=$trees['OptiScaler']
 }
 $receiptPath=Join-Path $Context.output 'build-receipt.json';Assert-NeuRoticOwnedOutput $Context.repository $receiptPath
 $receipt|ConvertTo-Json -Depth 30|Set-Content -LiteralPath $receiptPath -Encoding utf8
}
