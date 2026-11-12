# TinyBBS server — phases 1–2

Phase 1 supplies Linux implementations of the Arduino-shaped platform API so
the shared `src/bbs_core.h` can run unchanged. Phase 2 adds `tinybbs-server`:
the long-running shared-core loop. Phase 3 binds HTTP and WebSocket through
CivetWeb; Telnet remains phase 4.

On Rocky Linux, install the development prerequisites from the deployment
plan (including `civetweb-devel` for phase 3), then build the compatibility
check:

```sh
cmake -S server -B server/build
cmake --build server/build
```

Run it against a disposable copy of a folder that contains `BBS/`:

```sh
./server/build/tinybbs-phase1-smoke /srv/tinybbs-test SYSOP your-password
```

The optional handle/password arguments prove the server's mbedTLS SHA-256
calculation matches an existing copied card account. `bbsBegin()` performs
the same storage probe and clock initialisation as the stick, so never use a
live mounted SD card or production tree for this check.

`server/platform/miniz*` is vendored upstream miniz only for the `tinfl_*`
inflater used by the shared ZIP reader. The server target links
`miniz_tinfl.c`; the other headers provide its unchanged public API.

## Run the phase 2 process

Use a copied card tree while testing because the server runs the normal BBS
storage setup and clock persistence:

```sh
./server/build/tinybbs-server --root /srv/tinybbs-test \
  --http 127.0.0.1:8080 --telnet 0.0.0.0:2323
```

`--root` defaults to `/srv/tinybbs`; `--http` defaults to `127.0.0.1:8080`;
leaving out `--telnet` disables it. The process serves the same terminal and
SysOp pages from `shared/web/`, including polling, WebSocket, files, media and
uploads. Stop with Ctrl-C or `systemctl stop` once the service unit exists.
The process logs the settings summary then ticks the BBS every two milliseconds
under `BbsLock`.

For the server's configured local time, set `clock_sync=system` in that server copy's
`BBS/CONFIG/SYSTEM.INI`. It follows the host clock but shifts it by 28-year
steps into the `epoch=` era, preserving the 1990s date and matching weekday.
The repository's stick configuration remains `clock_sync=browser`.
