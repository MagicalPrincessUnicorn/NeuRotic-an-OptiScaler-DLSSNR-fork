[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Path,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9.]+$')]
    [string]$Key,

    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$Section = '',

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$Value,

    [ValidatePattern('^[A-Za-z0-9._-]+$')]
    [string]$ExpectedValue = 'auto'
)

$ErrorActionPreference = 'Stop'

try {
    $resolvedPath = (Resolve-Path -LiteralPath $Path).Path
    $bytes = [System.IO.File]::ReadAllBytes($resolvedPath)

    $hasUtf8Bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and
        $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $hasUtf16LeBom = $bytes.Length -ge 2 -and $bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE
    $hasUtf16BeBom = $bytes.Length -ge 2 -and $bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF

    if ($hasUtf8Bom) {
        $encoding = New-Object System.Text.UTF8Encoding($true, $true)
    }
    elseif ($hasUtf16LeBom) {
        $encoding = New-Object System.Text.UnicodeEncoding($false, $true, $true)
    }
    elseif ($hasUtf16BeBom) {
        $encoding = New-Object System.Text.UnicodeEncoding($true, $true, $true)
    }
    else {
        $strictUtf8 = New-Object System.Text.UTF8Encoding($false, $true)
        try {
            [void]$strictUtf8.GetString($bytes)
            $encoding = $strictUtf8
        }
        catch [System.Text.DecoderFallbackException] {
            $encoding = [System.Text.Encoding]::Default
        }
    }

    $text = $encoding.GetString($bytes)
    if ($hasUtf8Bom -or $hasUtf16LeBom -or $hasUtf16BeBom) {
        $text = $text.TrimStart([char]0xFEFF)
    }

    $scopeStart = 0
    $scopeLength = $text.Length

    if ($Section) {
        $sectionPattern = '(?im)^\s*\[' + [regex]::Escape($Section) + '\]\s*(?:[;#].*)?$'
        $sectionMatch = [regex]::Match($text, $sectionPattern)
        if (-not $sectionMatch.Success) {
            throw "Could not find section '[$Section]' in $resolvedPath."
        }

        $scopeStart = $sectionMatch.Index + $sectionMatch.Length
        $remainder = $text.Substring($scopeStart)
        $nextSection = [regex]::Match($remainder, '(?im)^\s*\[[^\]\r\n]+\]\s*(?:[;#].*)?$')
        $scopeLength = if ($nextSection.Success) { $nextSection.Index } else { $remainder.Length }
    }

    $scopeText = $text.Substring($scopeStart, $scopeLength)
    $pattern = '(?im)^(\s*' + [regex]::Escape($Key) + '\s*=\s*)([^\s;#]+)(\s*(?:[;#].*)?)$'
    $settingMatch = [regex]::Match($scopeText, $pattern)

    if (-not $settingMatch.Success) {
        $location = if ($Section) { " in section [$Section]" } else { '' }
        throw "Could not find '$Key'$location in $resolvedPath."
    }

    $currentValue = $settingMatch.Groups[2].Value
    if ($currentValue -ieq $Value) {
        Write-Host "$Key is already $Value; no INI change needed."
        exit 0
    }

    if ($currentValue -ine $ExpectedValue) {
        $location = if ($Section) { " in section [$Section]" } else { '' }
        throw "Refusing to change $Key${location}: expected '$ExpectedValue', found '$currentValue'."
    }

    $valueStart = $scopeStart + $settingMatch.Groups[2].Index
    $updated = $text.Substring(0, $valueStart) + $Value +
        $text.Substring($valueStart + $settingMatch.Groups[2].Length)

    [System.IO.File]::WriteAllText($resolvedPath, $updated, $encoding)
    $location = if ($Section) { " in [$Section]" } else { '' }
    Write-Host "Updated $Key=$Value$location without changing the INI text encoding."
    exit 0
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
