[CmdletBinding(SupportsShouldProcess)]
param(
  [string]$Manifest = (Join-Path $PSScriptRoot 'kouvosto-collection.json'),
  [string]$BbsRoot = (Join-Path $PSScriptRoot '..\sd\BBS'),
  [switch]$Publish
)

$ErrorActionPreference = 'Stop'
if (-not $Publish) { throw 'Use -Publish to fetch the explicit curated collection.' }
$manifestData = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json
$indexDir = Join-Path $BbsRoot 'DATA\INDEX'
$tempDir = Join-Path $BbsRoot 'TEMP\IMPORT'
New-Item -ItemType Directory -Force -Path $indexDir, $tempDir | Out-Null
$records = [System.Collections.Generic.List[object]]::new()

function Get-ZipDescription([string]$Path) {
  try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($Path)
    try {
      $entry = $zip.Entries | Where-Object { $_.FullName -match '(?i)(^|/)(FILE_ID\.DIZ|DESC\.SDI|DESCRIPT\.ION)$' } | Select-Object -First 1
      if (-not $entry -or $entry.Length -gt 8192) { return $null }
      $stream = $entry.Open()
      try {
        $buffer = New-Object byte[] $entry.Length
        [void]$stream.Read($buffer, 0, $buffer.Length)
        return (([Text.Encoding]::GetEncoding(437).GetString($buffer) -replace [char]0, '') -split '\r?\n' | Select-Object -First 40) -join [Environment]::NewLine
      } finally { $stream.Dispose() }
    } finally { $zip.Dispose() }
  } catch { return $null }
}

foreach ($item in $manifestData.items) {
  if ($item.target_area -notmatch '^\d{3}_[A-Z0-9_]+$' -or $item.file -match '[\\/:*?"<>|]' -or $item.file.Length -gt 120) { throw "Unsafe manifest item: $($item.file)" }
  $targetDir = Join-Path $BbsRoot (Join-Path 'FILES' $item.target_area)
  $target = Join-Path $targetDir $item.file
  if ($PSCmdlet.ShouldProcess($target, "Import $($item.file) into Kouvosto")) {
    New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
    if (-not (Test-Path -LiteralPath $target)) {
      $part = Join-Path $tempDir ($item.file + '.part')
      Invoke-WebRequest -Uri $item.archive_url -Headers @{ 'User-Agent'='Kouvosto BBS curator/0.1' } -OutFile $part
      Move-Item -LiteralPath $part -Destination $target
    }
    $records.Add([pscustomobject]@{
      filename=$item.file; target_area=$item.target_area; kind=$item.kind; source='Metropoli BBS archive'
      source_url=$item.source_url; archive_url=$item.archive_url
      sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
      bytes=(Get-Item -LiteralPath $target).Length
      description=$(if ([IO.Path]::GetExtension($target).ToUpperInvariant() -eq '.ZIP') { Get-ZipDescription $target } else { $null })
      imported_at=(Get-Date).ToUniversalTime().ToString('o')
    })
  }
}
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $indexDir 'KOUVOSTO_ORIGINS.json') -Encoding utf8
Write-Host "Imported $($records.Count) curated files into Kouvosto areas. Provenance is private index metadata only."
