[CmdletBinding(SupportsShouldProcess)]
param(
  [Parameter(Mandatory)] [string]$Identifier,
  [Parameter(Mandatory)] [string]$File,
  [string]$Destination = (Join-Path $PSScriptRoot '..\sd\BBS\UPLOADS\INCOMING\INTERNET')
)

$ErrorActionPreference = 'Stop'
if ($Identifier -notmatch '^[A-Za-z0-9_.-]+$' -or $File -match '(^|[\\/])\.\.([\\/]|$)') { throw 'Unsafe item identifier or file name.' }
$metadata = Invoke-RestMethod -Uri "https://archive.org/metadata/$Identifier"
$licence = "$($metadata.metadata.licenseurl) $($metadata.metadata.rights)"
if ($licence -notmatch '(?i)(publicdomain|creativecommons\.org/publicdomain|creativecommons\.org/licenses/by|cc0|creative commons attribution)') {
  throw 'Refusing download: metadata does not state an allowed public-domain or CC-BY style licence.'
}
$known = $metadata.files | Where-Object { $_.name -eq $File } | Select-Object -First 1
if (-not $known) { throw 'Requested file is not listed in Internet Archive item metadata.' }
$safeName = Split-Path -Leaf $File
$target = Join-Path $Destination $safeName
if ($PSCmdlet.ShouldProcess($target, "Download Internet Archive item $Identifier")) {
  New-Item -ItemType Directory -Force -Path $Destination | Out-Null
  Invoke-WebRequest -Uri "https://archive.org/download/$Identifier/$([uri]::EscapeDataString($File))" -OutFile $target
  Write-Host "Downloaded to staging: $target"
}
