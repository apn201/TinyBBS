# VolaMUD

VolaMUD is a separate native TinyBBS door. Legend of Kouvola remains its own
daily-turn game with its own save file, `KOUVQUEST.DAT`. VolaMUD stores one
character record per TinyBBS account in `VOLAMUD.DAT`.

The implementation is C++ for the ESP32 Arduino build. It does not embed a Go
runtime, GoMud server, web administration UI, WebSocket server or JavaScript
scripting engine.

## Source port

The initial VolaMUD world ports GoMud's default Tutorial zone, rooms 900 to
903. It keeps the room order and directional topology. The native version has
the same tutorial order: look around, inspect the character, fight a training
opponent, then finish the tutorial.

Source: https://github.com/GoMudEngine/GoMud at commit
`b3f55cc236ea67ac8827a62c68de7980474f0757`.

GoMud is MIT-licensed. The required notice is in `THIRD_PARTY_NOTICES.md`.

## Native engine boundary

The current port has:

- persistent character records;
- shared room presence and room chat across browser and Telnet callers;
- directional exits and `LOOK`, `GO`, `WHO`, `SAY`, `STATS` and `QUIT`;
- the tutorial combat encounter with HP, experience, coins and level growth.

The next port steps are room data loading from SD, inventory, NPC instances,
combat data, quests, world ticks and additional GoMud zones. Those systems
must be designed for TinyBBS's eight-node pool and bounded SD/RAM use. Do not
copy GoMud's server or client stack into the firmware.
