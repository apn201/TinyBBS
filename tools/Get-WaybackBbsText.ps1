[CmdletBinding(SupportsShouldProcess)]
param(
  [Parameter(Mandatory)] [string]$Capture,
  [Parameter(Mandatory)] [string]$OriginalDirectory,
  [Parameter(Mandatory)] [string[]]$File,
  [string]$Destination = (Join-Path $PSScriptRoot '..\sd\BBS\UPLOADS\INCOMING\INTERNET\WAYBACK'),
  [switch]$AllowArchives
)

$ErrorActionPreference = 'Stop'
if ($Capture -notmatch '^\d{14}$') { throw 'Capture must be a 14-digit Wayback timestamp.' }
if ($OriginalDirectory -notmatch '^https?://[^ ]+/$') { throw 'OriginalDirectory must be an http(s) directory URL ending in slash.' }
New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$provenance = [System.Collections.Generic.List[object]]::new()
foreach ($name in $File) {
  if ($name -notmatch '^[A-Za-z0-9._%#-]+$') { throw "Unsafe file name: $name" }
  $extension = [IO.Path]::GetExtension($name).ToUpperInvariant()
  if (-not $AllowArchives -and $extension -notin '.TXT', '.DOC', '.NFO', '.DIZ', '.FAQ') {
    throw "Refusing non-text file without -AllowArchives: $name"
  }
  $url = "https://web.archive.org/web/$($Capture)id_/$OriginalDirectory$name"
  $target = Join-Path $Destination $name
  if ($PSCmdlet.ShouldProcess($target, "Stage Wayback capture $name")) {
    Invoke-WebRequest -Uri $url -Headers @{ 'User-Agent'='TinyBBS curator/0.1 personal archive' } -OutFile $target
    $provenance.Add([pscustomobject]@{
      filename=$name; source='Wayback Machine'; capture=$Capture; original_url=($OriginalDirectory + $name)
      archive_url=$url; rights='unknown; staging only'; sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
      staged_at=(Get-Date).ToUniversalTime().ToString('o')
    })
  }
}
$provenance | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'WAYBACK_PROVENANCE.json') -Encoding utf8
Write-Host "Staged $($provenance.Count) capture(s) with provenance. No file was published."
