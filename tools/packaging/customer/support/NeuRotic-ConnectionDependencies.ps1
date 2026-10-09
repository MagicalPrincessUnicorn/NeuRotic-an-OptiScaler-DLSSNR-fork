# Optional dependencies join the existing setup engine transaction; this module
# never writes game files or downloads/bundles third-party wrappers.
function Get-ConnectionPEBits([string]$Path) {
 $stream=[IO.File]::OpenRead($Path);$reader=New-Object IO.BinaryReader($stream)
 try {
  if($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d){throw 'Dependency is not a PE file'}
  $stream.Position=60;$offset=$reader.ReadUInt32()
  if($offset -gt $stream.Length-26){throw 'Invalid PE header offset'}
  $stream.Position=$offset;if($reader.ReadUInt32() -ne 0x4550){throw 'Invalid PE signature'}
  $machine=$reader.ReadUInt16();$stream.Position=$offset+24;$magic=$reader.ReadUInt16()
  if($machine -eq 0x14c -and $magic -eq 0x10b){return 32}
  if($machine -eq 0x8664 -and $magic -eq 0x20b){return 64}
  throw 'Unsupported PE architecture'
 }finally{$reader.Dispose();$stream.Dispose()}
}
function Plan-ConnectionInstall([string]$TargetExe,[string]$Selection,[string]$DependencyManifest,[string]$ProxyName) {
 if($Selection -notin @('DXVK','dgVoodoo2')){throw 'Select DXVK or dgVoodoo2 explicitly'}
 $exe=[IO.Path]::GetFullPath($TargetExe);$root=Split-Path -Parent $exe
 [void](SafePath $root (Split-Path -Leaf $exe))
 $manifestPath=[IO.Path]::GetFullPath($DependencyManifest);$dependencyRoot=Split-Path -Parent $manifestPath
 [void](SafePath $dependencyRoot (Split-Path -Leaf $manifestPath))
 $m=Get-Content -Raw -LiteralPath $manifestPath|ConvertFrom-Json
 if($m.schema -ne 1 -or $m.identity -ne $Selection -or -not $m.version -or $m.licenseStatus -ne 'user-supplied') {throw 'An exact version and user-supplied license acknowledgement are required'}
 $bits=Get-ConnectionPEBits $exe
 if($m.bitness -ne $bits){throw 'Dependency bitness does not match target executable'}
 if(($Selection -eq 'DXVK' -and $m.outputApi -ne 'Vulkan') -or ($Selection -eq 'dgVoodoo2' -and $m.outputApi -ne 'D3D11')){throw 'Unexpected translator output API'}
 if($m.inputApi -notin @('D3D8','D3D9') -or ($m.inputApi -eq 'D3D8' -and $bits -ne 32)){throw 'Only D3D8 x86 and D3D9 x86/x64 are eligible for explicit testing'}
 $expected=if($m.inputApi -eq 'D3D8'){'d3d8.dll'}else{'d3d9.dll'}
 $entries=@();$names=@{}
 foreach($f in $m.files) {
  if($f.path -notin @($expected,'dgVoodoo.conf') -or $names.ContainsKey([string]$f.path)){throw 'Unexpected or duplicate wrapper file'}
  $names[[string]$f.path]=$true
  if($f.sha256 -notmatch '^[a-fA-F0-9]{64}$'){throw 'Exact dependency SHA256 is required'}
  $source=SafePath $dependencyRoot $f.path;$target=SafePath $root $f.path
  if((HashFile $source) -ne $f.sha256){throw 'Dependency hash mismatch'}
  if($f.path -eq $expected -and (Get-ConnectionPEBits $source) -ne $bits){throw 'Wrapper binary bitness mismatch'}
  if($f.path -ieq $ProxyName){throw 'Wrapper collides with NeuRotic proxy'}
  $exists=Test-Path -LiteralPath $target -PathType Leaf
  if((Test-Path -LiteralPath $target) -and -not $exists){throw 'Wrapper destination is not a regular file'}
  $prior=if($exists){HashFile $target}else{$null}
  if($exists -and $f.path -ne 'dgVoodoo.conf' -and $prior -ne $f.sha256){throw 'A foreign wrapper occupies the proxy; preserve it and select a compatible chain explicitly'}
  $entries += [pscustomobject]@{path=$f.path;source=$source;operation=$(if($exists){'preserve-existing'}else{'copy-user-dependency'});existed=$exists;previous_hash=$prior;installed_hash=$(if($exists){$prior}else{$f.sha256})}
 }
 if(-not $names.ContainsKey($expected)){throw 'Manifest lacks the selected wrapper binary'}
 foreach($proxy in @('d3d8.dll','d3d9.dll','dxgi.dll','opengl32.dll')) {
  if($proxy -eq $expected -or $proxy -ieq $ProxyName){continue}
  if(Test-Path -LiteralPath (SafePath $root $proxy)){throw "Existing proxy chain needs explicit compatibility review: $proxy"}
 }
 [pscustomobject]@{target=$exe;targetHash=(HashFile $exe);bitness=$bits;root=$root;identity=$m.identity;version=$m.version;restartRequired=$true;files=$entries}
}
function Test-ConnectionPreimage($Plan) {
 if((HashFile $Plan.target) -ne $Plan.targetHash -or (Get-ConnectionPEBits $Plan.target) -ne $Plan.bitness){throw 'Target executable changed after planning'}
 foreach($f in $Plan.files) {
  $target=SafePath $Plan.root $f.path;$exists=Test-Path -LiteralPath $target -PathType Leaf
  if($exists -ne $f.existed -or ($exists -and (HashFile $target) -ne $f.previous_hash)){throw 'Wrapper destination changed after planning'}
  if($f.operation -eq 'copy-user-dependency' -and (HashFile $f.source) -ne $f.installed_hash){throw 'Dependency changed after planning'}
 }
}
