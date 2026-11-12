# Kouvosto archive policy

Kouvosto is a personal offline BBS archive, not an indiscriminate mirror.
Every imported file receives a source, SHA-256, target area, review status and,
where present, FILE_ID.DIZ/DESC.SDI/DESCRIPT.ION text in the generated seed
catalogue.

## Staging and release

All material starts in BBS/UPLOADS/INCOMING. Staging does not make a file
callable or downloadable. A SysOp approves a reviewed file into a public area
or one of the private areas:

- 013_THE_BACK_ROOM: security level 50 plus BACKROOM flag.
- 014_SYSOP_VAULT: security level 255 plus SYSOP flag.

Lyrics, commercial games, executable bundles and material with unclear rights
remain under review. Never execute archived programs on the BBS host while
cataloguing them.

## Internet Archive source notes

Internet Archive's Metadata API is used for provenance and file listings.
The included discovery/downloader tools only automatically accept records whose
metadata explicitly states public-domain, CC0, or CC-BY-style terms.

Metropoli BBS is a valuable Finnish reference source:

- metropoli-bbs: presentation item and screenshot.
- files.mpoli.fi.2013.04: 2013 FILES.MPOLI.FI capture; roughly 11.5 GB ZIP.
- MetropoliBBSFiles: 2018 capture; roughly 19.4 GB ZIP.

As of 2026-11-10, their Internet Archive item metadata does not declare a
licence or rights statement. The Kouvosto SysOp has explicitly authorised a
small, selected import from Metropoli directory structures. That permission is
recorded in Kouvosto's private provenance index; it is not a claim that the
full mirrors are public-domain or a reason to mirror them.

## Period conventions

Keep original filenames and timestamps when practical. Prefer the original
archive container. File descriptions are period metadata, not decoration:
recognise FILE_ID.DIZ, DESC.SDI, DESCRIPT.ION, ZIP and ARJ, preserve CP437
where possible, and keep generated modern data outside caller-facing screens.

## FidoNet message history

`tools/fidonet-seed.py` builds `/BBS/MSGBASE` from the ExecPC BBS FidoNet
capture (1993-1999) at
[Break Into Chat](https://breakintochat.com/collections/messages/fidonet/),
archived by Josh Renaud. The source ZIP stays in `tools/.cache` on the PC and
never goes on the card.

- Echoes about health, recovery, politics and similar personal subjects are
  left out.
- Every name in From and To, and every name of anyone who posted in the chosen
  echoes, is replaced with an invented one wherever it appears. The same
  person always gets the same invented name, so threads still read naturally.
  Quote initials follow the invented names.
- Node addresses, BBS names in origin lines, phone numbers, e-mail and web
  addresses are replaced too. Tear lines (the mail software) and taglines are
  kept as period colour.
- Public figures discussed in the messages, such as comic artists or actors,
  keep their names.
- The text is otherwise as written, including typos.

## Browser modem audio

`/BBS/ASSETS/DIALUP.MP3` is the user-selected nostalgia asset for the browser
terminal's opt-in MODEM AUDIO button. Source: https://oona.windytan.com/blogfiles/dialup.mp3
The explanatory reference is https://www.windytan.com/2012/11/the-sound-of-dialup-pictured.html
