# TinyBBS roadmap

The current repository is a working vertical slice. It boots an AP, serves a
browser terminal, accepts Telnet, stores accounts and messages on SD, and lets
callers see each other in the same BBS. It is not the complete 1.0 described
in `TinyBBS_READY_TO_CODE_SPEC.md`.

## Done in the current slice

- ESP32-S3, ST7735 and SD_MMC bring-up using the TinyIRC pin contract.
- Isolated AP, captive DNS, HTTP polling terminal and Telnet.
- Shared eight-node session pool.
- Login, new-user flow, salted SHA-256 password records and persistent user
  records.
- Message areas, basic conferences, chat, WHO, SysOp page, event logs and LCD
  status.
- SD-hosted ANSI, ASCII, bulletins, configuration and modem audio.
- Paged ANSI and text views, plus a browser-only listed-file download route.
- Separate native Legend of Kouvola and VolaMUD doors. VolaMUD has a static
  city hub, an SD-defined deterministic wilderness, visible seeded/random
  encounters, persistent equipment, NPC talk, trading and small tasks. Its
  gold and bank are shared with Legend and the dice cabinet.

The browser download route should be treated as an early convenience feature.
Uploads, approval queues, archive indexing, Telnet transfer protocols and
hardware performance checks still need work.

## Next work

1. Replace the browser polling carrier with WebSocket transport and reconnect
   tokens. Keep browser and Telnet sessions inside the same session manager.
2. Profile AP, SD and terminal behavior on the physical ESP32-S3. Test the
   eight active BBS-node target before increasing any limit.
3. Build safe file-area handling: generated indexes, opaque transfer tokens,
   streamed browser downloads, upload staging, SysOp approval, accounting and
   ratio tests. Do not expose SD paths.
4. Add QWK export and reply import as streamed SD work, with checkpoints and
   at least one desktop-reader interoperability test.
5. Extract the native doors into a common door interface, then add richer task
   chains, NPC state, a proper equipment/trading screen and more VolaMUD world data one part at a
   time. Do not claim DOS-door compatibility.
6. Only after the local board is profiled, consider explicit AP plus station
   mode, LAN access, SSH feasibility or modem protocols.

## Things that remain deliberately incomplete

- WebSocket transport and reconnect.
- Caller uploads and SysOp approval queues.
- Finished file-transfer support and Telnet transfer protocols.
- QWK packet creation and reply import.
- General door API and additional doors.
- Measured concurrency, transfer fairness and power-loss behavior.
- AP plus station, LAN, WAN, SSH and modem support.

`NETWORK.INI` contains future station settings but release 0.1 must remain
offline and AP-only. Do not use that file as permission to connect the board
to another network.

## Content and archive work

Kouvosto archive material goes to its native Kouvosto areas. Preserve original
archive names and FILE_ID.DIZ, DESC.SDI and DESCRIPT.ION data. Keep provenance
in `/BBS/DATA/INDEX/KOUVOSTO_ORIGINS.json`, not in a caller-visible MPOLI
folder. See `CURATION.md` before importing material.

Never populate generated data, logs, temporary areas or QWK work directories
by hand. Use the curator scripts in `tools` for archive ingest.
