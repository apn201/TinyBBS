# VolaMUD

VolaMUD is a separate native TinyBBS door. It is written in C++ for the ESP32;
it does not embed a Go runtime, server, web UI or scripting engine.

## Shared world

Vola City is fixed. Its gate, Packet Square, temple, tinker, training academy,
armory, tavern, archive, bank and roads live in
`/BBS/DOORS/VOLAMUD/WORLD.TXT`. Everything beyond the city is deterministic:
the ESP derives a room from `worldX`, `worldY` and the seed in
`/BBS/CONFIG/VOLAMUD.INI`. The same coordinate means the same place for every
caller. There is no room database in firmware and no generated map stored on
the SD card.

The SD dictionary supplies place names, landscapes, scenes, details, NPCs,
enemies, dialogue and task flavour. This keeps prose and world expansion out
of the firmware image. Do not put a pipe character in a dictionary value.

## Play

`LOOK`, `N`, `S`, `E`, `W`, `GO <direction>`, `SAY <text>`, `WHO`, `FIGHT`,
`STATS` and `QUIT` work as expected. City locations are cyan and wilderness
locations green. Wilderness rooms are normally empty: a seeded fixed monster
or a session-local skeleton, orc or goblin is announced before it can be
fought. `FIGHT NPC` is possible, but city people are deliberately formidable.

`TALK` provides city services, local flavour or occasional wilderness
directions. `TASK` only works with a scout, guild member, archivist or a rare
wilderness traveller. The archive's special task is to bring back the Black
Crown of Kouvosto from Lord Korja at `13,-8`.

`INV`, `WEAR <item>` and `SELL <item>` manage persistent loot. Worn rings,
amulets and artefacts modify HP or combat; Tinker buys most loot. At the Bank
of Kouvosto (`-1,-2`), use `BANK`, `BANK DEPOSIT <gold>` or `BANK WITHDRAW
<gold>`.

Gold and bank balance are deliberately shared between VolaMUD, Legend of
Kouvola and the dice cabinet. `WALLET.DAT` is the authoritative purse. The
first visit migrates a caller's existing Legend purse into it. Character HP,
level, XP, location and task remain in `VOLAMUD.DAT`.

## Origin and limits

GoMud was inspected as an MIT-licensed mechanics/world reference, but this
version ships neither its Go server nor copied GoMud world data. The current
Vola City and procedural wilderness are original TinyBBS content. The
attribution record remains in `THIRD_PARTY_NOTICES.md` for provenance.

VolaMUD is designed for TinyBBS's eight shared BBS nodes. It has a deliberately
small equipment set and a few city shops, not a full inventory/trading system.
Dynamic NPC state, party mechanics, a deeper economy and long multi-stage
quests remain future work.
