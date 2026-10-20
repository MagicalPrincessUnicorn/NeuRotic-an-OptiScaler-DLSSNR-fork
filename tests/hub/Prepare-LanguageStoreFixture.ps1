$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$fixture=[IO.Path]::GetFullPath((Join-Path $repo 'builds/hub/language-store/fixture'))
if(-not $fixture.StartsWith([IO.Path]::GetFullPath($repo).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected fixture root'}
if(Test-Path -LiteralPath $fixture){
 $pending=New-Object 'Collections.Generic.Stack[string]';$pending.Push($fixture)
 while($pending.Count){$directory=$pending.Pop();$item=Get-Item -LiteralPath $directory;if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'};foreach($child in Get-ChildItem -LiteralPath $directory -Force){if($child.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked fixture cleanup refused'};if($child.PSIsContainer){$pending.Push($child.FullName)}}}
 Remove-Item -LiteralPath $fixture -Recurse -Force
}
