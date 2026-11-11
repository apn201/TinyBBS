[CmdletBinding(SupportsShouldProcess)]
param(
  [string]$MapFile = (Join-Path $PSScriptRoot 'seed-areas.json'),
  [string]$BbsRoot = (Join-Path $PSScriptRoot '..\sd\BBS'),
  [switch]$Stage,
  [switch]$Publish,
  [string[]]$Area,
  [switch]$IncludeRightsReview
)

$ErrorActionPreference = 'Stop'
$indexDir = Join-Path $BbsRoot 'DATA\INDEX'
$incomingRoot = Join-Path $BbsRoot 'UPLOADS\INCOMING\SEED'
$map = Get-Content -LiteralPath $MapFile -Raw | ConvertFrom-Json
$sevenZip = @('C:\Program Files\7-Zip\7z.exe', 'C:\Program Files (x86)\7-Zip\7z.exe') | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1

function Get-SafeName([string]$Name) {
  $clean = $Name -replace '[\\/:*?"<>|]', '_'
  if ($clean.Length -gt 120) { $clean = $clean.Substring(0, 120) }
  return $clean
}

function Get-Cp437Text([byte[]]$Bytes) {
  $text = [System.Text.Encoding]::GetEncoding(437).GetString($Bytes)
  return (($text -replace [char]0, '') -split '\r?\n' | Select-Object -First 40) -join [Environment]::NewLine
}

function Get-ZipDescription([System.IO.FileInfo]$File) {
  try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($File.FullName)
    try {
      $entry = $zip.Entries | Where-Object { $_.FullName -match '(?i)(^|/)(FILE_ID\.DIZ|DESC\.SDI|DESCRIPT\.ION)$' } | Select-Object -First 1
      if (-not $entry -or $entry.Length -gt 8192) { return $null }
      $stream = $entry.Open()
      try {
        $buffer = New-Object byte[] $entry.Length
        [void]$stream.Read($buffer, 0, $buffer.Length)
        return Get-Cp437Text $buffer
      } finally { $stream.Dispose() }
    } finally { $zip.Dispose() }
  } catch { return $null }
}

function Get-ArjDescription([System.IO.FileInfo]$File) {
  if (-not $sevenZip) { return $null }
  try {
    $result = & $sevenZip e -so -- $File.FullName FILE_ID.DIZ 2>$null | Out-String
    if ($LASTEXITCODE -eq 0 -and $result.Length -le 8192) { return $result.Trim() }
  } catch {}
  return $null
}

function Get-Description([System.IO.FileInfo]$File) {
  if ($File.Name -match '(?i)^(FILE_ID\.DIZ|DESC\.SDI|DESCRIPT\.ION)$' -and $File.Length -le 8192) {
    return Get-Cp437Text ([System.IO.File]::ReadAllBytes($File.FullName))
  }
  if ($File.Extension -ieq '.zip') { return Get-ZipDescription $File }
  if ($File.Extension -ieq '.arj') { return Get-ArjDescription $File }
  return $null
}

New-Item -ItemType Directory -Force -Path $indexDir | Out-Null
$records = [System.Collections.Generic.List[object]]::new()
foreach ($entry in $map.areas) {
  $selected = -not $Area -or $entry.name -in $Area
  if (-not (Test-Path -LiteralPath $entry.source -PathType Container)) { Write-Warning "Missing seed source: $($entry.source)"; continue }
  $files = Get-ChildItem -LiteralPath $entry.source -Recurse -File
  foreach ($file in $files) {
    $relative = $file.FullName.Substring($entry.source.Length).TrimStart('\')
    $record = [ordered]@{
      source_area = $entry.name; source_relative = $relative; target_area = $entry.target; review = $entry.review
      filename = $file.Name; extension = $file.Extension.ToUpperInvariant(); bytes = $file.Length
      sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
      description = Get-Description $file; staged = $false; published = $false
    }
    if (($Stage -or $Publish) -and $selected) {
      $root = if ($Publish) { Join-Path $BbsRoot (Join-Path 'FILES' $entry.target) } else { Join-Path $incomingRoot $entry.name }
      $safeParts = ($relative -split '[\\/]') | ForEach-Object { Get-SafeName $_ }
      $destination = $root
      foreach ($part in $safeParts) { $destination = Join-Path $destination $part }
      if ($Publish -and $entry.review -match 'rights' -and -not $IncludeRightsReview) {
        $record.publish_blocked = 'rights review required'
      } elseif ($PSCmdlet.ShouldProcess($destination, "Copy $($file.Name)")) {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        if (-not (Test-Path -LiteralPath $destination)) { Copy-Item -LiteralPath $file.FullName -Destination $destination }
        if ($Publish) { $record.published = $true } else { $record.staged = $true }
      }
    }
    $records.Add([pscustomobject]$record)
  }
}
$catalog = Join-Path $indexDir 'SEED_CATALOG.json'
$records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $catalog -Encoding utf8
$summary = $records | Group-Object source_area | ForEach-Object {
  [pscustomobject]@{ area=$_.Name; files=$_.Count; bytes=($_.Group | Measure-Object bytes -Sum).Sum; target=$_.Group[0].target_area; review=$_.Group[0].review }
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $indexDir 'SEED_SUMMARY.json') -Encoding utf8
Write-Host "Catalogued $($records.Count) files: $catalog"
if ($Stage) { Write-Host "Staged under $incomingRoot; no caller can download these yet." }
if ($Publish) { Write-Host 'Published only selected allowed records; review the catalog before exposing any area.' }
