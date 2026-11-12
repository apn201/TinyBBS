# TinyBBS server — phase 1

Phase 1 supplies Linux implementations of the Arduino-shaped platform API so
the shared `src/bbs_core.h` can run unchanged. It does not start a server,
bind a network port or serve a web page; those belong to phases 2–4 in
[`docs/APACHE_PORT.md`](../docs/APACHE_PORT.md).

On Rocky Linux, install the development prerequisites from the deployment
plan, then build the compatibility check:

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
