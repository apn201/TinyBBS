param(
  [Parameter(Mandatory = $true)]
  [string]$CardRoot
)

$ErrorActionPreference = 'Stop'
$updater = Join-Path $PSScriptRoot 'tools\prepare-sd.ps1'
& $updater -CardRoot $CardRoot -PresentationOnly
