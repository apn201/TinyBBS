param(
  [Parameter(Mandatory = $true)]
  [ValidateScript({ Test-Path $_ -PathType Container })]
  [string]$CardRoot,
  [switch]$PresentationOnly
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\sd\BBS')).ProviderPath
$target = Join-Path (Resolve-Path -LiteralPath $CardRoot).ProviderPath 'BBS'

New-Item -ItemType Directory -Force -Path $target | Out-Null
if ($PresentationOnly) {
  foreach ($folder in 'ANSI', 'ASCII', 'ASSETS', 'BULLETINS') {
    $dest = Join-Path $target $folder
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $source $folder) -Force | ForEach-Object {
      Copy-Item -LiteralPath $_.FullName -Destination $dest -Recurse -Force
    }
  }
  $doorSource = Join-Path $source 'DOORS'
  if (Test-Path -LiteralPath $doorSource) {
    Copy-Item -LiteralPath $doorSource -Destination $target -Recurse -Force
  }
  Copy-Item -LiteralPath (Join-Path $source 'CONFIG\VOLAMUD.INI') -Destination (Join-Path $target 'CONFIG\VOLAMUD.INI') -Force
  Write-Host "Updated presentation, VolaMUD world data and its seed at $target"
  return
}
Get-ChildItem -LiteralPath $source -Force | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination $target -Recurse -Force
}
$required = @(
  'FILES\001_BBS_SOFTWARE', 'FILES\004_GAMES', 'FILES\005_DEMOS',
  'FILES\006_ANSI_ASCII', 'FILES\008_PROGRAMMING', 'FILES\010_TEXTFILES',
  'FILES\011_UPLOADS', 'FILES\012_SYSOP', 'FILES\013_IMAGES',
  'FILES\013_THE_BACK_ROOM', 'FILES\014_BACKROOM_IMAGES', 'FILES\014_SYSOP_VAULT',
  'UPLOADS\INCOMING', 'DATA\INDEX'
)
foreach ($relative in $required) { New-Item -ItemType Directory -Force -Path (Join-Path $target $relative) | Out-Null }
Write-Host "TinyBBS presentation/config files copied to $target"
Write-Host 'Merge the supplied TinyBBS_SD_CARD_EMPTY_TREE.zip first if you want every reserved archive directory.'
