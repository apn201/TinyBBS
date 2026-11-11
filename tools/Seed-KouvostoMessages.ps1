[CmdletBinding(SupportsShouldProcess)]
param(
  [string]$BbsRoot = (Join-Path $PSScriptRoot '..\sd\BBS'),
  [switch]$Force
)

$ErrorActionPreference = 'Stop'
$path = Join-Path $BbsRoot 'DATA\MSGS\MESSAGES.DAT'
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $path) | Out-Null
if ((Test-Path -LiteralPath $path) -and (Get-Item -LiteralPath $path).Length -gt 0 -and -not $Force) {
  throw 'Message base already contains records. Use -Force only when replacing a disposable seed.'
}
function Encode-Record([string]$Value) {
  return $Value.Replace('%', '%25').Replace('|', '%7C').Replace([string][char]13, '%0D').Replace([string][char]10, '%0A')
}
$messages = @(
  @{ c=0; f='SYSOP'; s='Kouvosto comes online'; b='This is the Kouvosto opening archive. The board is new; the feeling is deliberately old. Leave a message, read a textfile, and do not feed the modem after midnight.' },
  @{ c=0; f='NIGHTSHIFT'; s='The Back Room?'; b='I heard there is another conference after General. Probably just a rumour. Probably.' },
  @{ c=0; f='BYTEBANDIT'; s='ANSI screens wanted'; b='If you have original screens, old packs or your own work, upload them with a proper FILE_ID.DIZ. No DIZ, no glory.' },
  @{ c=0; f='TURBO'; s='One gig is plenty'; b='A gigabyte would have been a ridiculous hard drive. Fill it with text, source and strange little utilities before filling it with anything else.' },
  @{ c=1; f='KALLE'; s='Favourite DOS command?'; b='Mine is still DIR /W. It tells the truth in columns.' },
  @{ c=1; f='MIRAGE'; s='BASIC never left'; b='The old code is still readable. That is more than I can say for most things built last week.' },
  @{ c=1; f='ACIDBURN'; s='What belongs on a local board'; b='Local maps. Local jokes. Code that fits on a floppy. And somebody awake at 02:00.' },
  @{ c=1; f='PIXEL'; s='CP437 appreciation society'; b='The block characters are not decoration. They are architecture.' },
  @{ c=1; f='GHOSTLINE'; s='Textfiles'; b='A good textfile needs a filename you remember, a header you do not understand, and one useful paragraph hidden near the end.' },
  @{ c=1; f='GREYBOX'; s='Archive etiquette'; b='Keep the original name. Keep the DIZ. Add provenance. Do not turn a file area into a mystery meat folder.' },
  @{ c=2; f='SYSOP'; s='Back Room keyholders'; b='This conference is seeded fiction for the opening archive. The actual door remains invite-only.' },
  @{ c=2; f='NIGHTSHIFT'; s='Signal check'; b='If you can read this, the key works. Keep the good stuff good.' }
)
if ($PSCmdlet.ShouldProcess($path, 'Write Kouvosto opening-archive messages')) {
  $lines = for ($i = 0; $i -lt $messages.Count; $i++) {
    $m = $messages[$i]
    ($i + 1).ToString() + '|' + $m.c + '|' + (Encode-Record $m.f) + '||' + (Encode-Record $m.s) + '|OPENING ARCHIVE|' + (Encode-Record $m.b)
  }
  Set-Content -LiteralPath $path -Value $lines -Encoding ascii
  Write-Host "Wrote $($messages.Count) opening-archive messages to $path"
}
