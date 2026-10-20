param([switch]$Remove)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$external=Join-Path $repo 'external'
$source='C:/NeuRotic/Optiscaler-Stuff/external'
foreach($name in @('FidelityFX-SDK','FidelityFX-SDK-v2','magic_enum','nvapi','simpleini','spdlog','unordered_dense','vulkan','xess')) {
    $path=[IO.Path]::GetFullPath((Join-Path $external $name))
    $target=[IO.Path]::GetFullPath((Join-Path $source $name))
    if(-not $path.StartsWith($external+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Path outside this checkout'}
    $item=Get-Item -LiteralPath $path -ErrorAction SilentlyContinue
    if($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        if([IO.Path]::GetFullPath([string]$item.Target) -ne $target){throw "Unowned link: $path"}
        if($Remove){
            # Delete only this exact directory entry; never recurse into its target.
            [IO.Directory]::Delete($path)
            New-Item -ItemType Directory -Path $path | Out-Null
        }
        continue
    }
    if($Remove){continue}
    if($item -and @(Get-ChildItem -LiteralPath $path -Force).Count){throw "Preserve existing dependency: $path"}
    if(-not(Test-Path -LiteralPath $target -PathType Container)){throw "Installed dependency missing: $target"}
    if($item){[IO.Directory]::Delete($path)}
    New-Item -ItemType Junction -Path $path -Target $target | Out-Null
}
Write-Output $(if($Remove){'Removed only Experimental dependency links; installed sources preserved.'}else{'Experimental dependencies reference installed sources; do not edit link targets.'})
