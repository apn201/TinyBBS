[CmdletBinding()]
param(
  [string]$Query = 'mediatype:software AND (subject:(BBS OR ANSI OR DOS) OR title:(BBS OR ANSI))',
  [int]$Rows = 50,
  [string]$Output = (Join-Path $PSScriptRoot '..\sd\BBS\DATA\INDEX\INTERNET_CANDIDATES.json')
)

$ErrorActionPreference = 'Stop'
$fields = @('identifier','title','year','licenseurl','rights','downloads','item_size')
$queryArgs = [System.Uri]::EscapeDataString($Query)
$fieldArgs = ($fields | ForEach-Object { 'fl[]=' + $_ }) -join '&'
$uri = "https://archive.org/advancedsearch.php?q=$queryArgs&$fieldArgs&rows=$Rows&page=1&output=json"
$result = Invoke-RestMethod -Uri $uri -Headers @{ 'User-Agent' = 'TinyBBS curator/0.1 personal offline archive' }
$allowed = $result.response.docs | Where-Object {
  $_.licenseurl -match '(?i)(publicdomain|creativecommons\.org/publicdomain|creativecommons\.org/licenses/by)' -or
  $_.rights -match '(?i)(public domain|cc0|creative commons attribution)'
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Output) | Out-Null
[pscustomobject]@{ query=$Query; fetched_at=(Get-Date).ToUniversalTime().ToString('o'); candidates=$allowed } |
  ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Output -Encoding utf8
Write-Host "Saved $($allowed.Count) rights-filtered candidates to $Output"
Write-Host 'Discovery only: review every item and its files before downloading.'
