# Portable profile validation only; imports never invoke a provider or activate rendering.
function Test-ObjectRuleSchemaNode($Value,$Schema){
 if($Schema.PSObject.Properties.Name -contains 'oneOf'){
  $matched=0;foreach($alternative in $Schema.oneOf){if(Test-ObjectRuleSchemaNode $Value $alternative){$matched++}};return $matched -eq 1
 }
 if($Schema.PSObject.Properties.Name -contains 'const' -and $Value -cne $Schema.const){return $false}
 if($Schema.PSObject.Properties.Name -contains 'enum' -and $Value -cnotin $Schema.enum){return $false}
 switch($Schema.type){
  'object' {
   if($Value -isnot [pscustomobject]){return $false}
   foreach($key in $Schema.required){if($key -cnotin $Value.PSObject.Properties.Name){return $false}}
   foreach($property in $Value.PSObject.Properties){if($property.Name -cnotin $Schema.properties.PSObject.Properties.Name){return $false};if(-not (Test-ObjectRuleSchemaNode $property.Value $Schema.properties.($property.Name))){return $false}}
  }
  'array' {
   if($Value -isnot [Array]){return $false};$count=$Value.Count
   if($Schema.PSObject.Properties.Name -contains 'minItems' -and $count -lt $Schema.minItems){return $false}
   if($Schema.PSObject.Properties.Name -contains 'maxItems' -and $count -gt $Schema.maxItems){return $false}
   foreach($item in $Value){if(-not (Test-ObjectRuleSchemaNode $item $Schema.items)){return $false}}
  }
  'string' {
   if($Value -isnot [string] -or $Value -match '[\x00-\x1f\x7f-\x9f]'){return $false}
   $count=0;for($i=0;$i -lt $Value.Length;$i++){if(-not [char]::IsLowSurrogate($Value[$i])){$count++}}
   if($Schema.PSObject.Properties.Name -contains 'minLength' -and $count -lt $Schema.minLength){return $false}
   if($Schema.PSObject.Properties.Name -contains 'maxLength' -and $count -gt $Schema.maxLength){return $false}
   if($Schema.PSObject.Properties.Name -contains 'pattern' -and $Value -cnotmatch $Schema.pattern){return $false}
  }
  'boolean' {if($Value -isnot [bool]){return $false}}
  {$_ -in @('number','integer')} {
   if($Value -isnot [int] -and $Value -isnot [long] -and $Value -isnot [double] -and $Value -isnot [decimal]){return $false}
   if($Schema.type -eq 'integer' -and $Value -isnot [int] -and $Value -isnot [long]){return $false}
   if([double]::IsNaN($Value) -or [double]::IsInfinity($Value)){return $false}
   if($Schema.PSObject.Properties.Name -contains 'minimum' -and $Value -lt $Schema.minimum){return $false}
   if($Schema.PSObject.Properties.Name -contains 'maximum' -and $Value -gt $Schema.maximum){return $false}
  }
 }
 return $true
}
function Test-ObjectRuleProfile($Profile){
 $schema=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'object-rules-v1.schema.json') | ConvertFrom-Json
 if(-not (Test-ObjectRuleSchemaNode $Profile $schema)){return $false}
 if([string]::IsNullOrWhiteSpace($Profile.profile.name)){return $false}
 $ids=@{};foreach($rule in $Profile.rules){
  if($ids.ContainsKey($rule.id) -or [string]::IsNullOrWhiteSpace($rule.label)){return $false};$ids[$rule.id]=$true
  if($rule.match.kind -ceq 'text'){foreach($phrase in $rule.match.phrases){if([string]::IsNullOrWhiteSpace($phrase.text) -or $phrase.language -cnotmatch '^[A-Za-z]{2,8}(-[A-Za-z0-9]{1,8})*$'){return $false}}}
  $controls=@{};foreach($control in $rule.requested_controls){if($controls.ContainsKey($control.id) -or $control.id -cmatch '^(overlay|nr|appearance)\.'){return $false};$controls[$control.id]=$true}
 }
 return $true
}
