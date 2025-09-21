Application launcher (`app`)

The `app` command loads small userland helpers that are linked against the in-kernel MezAPI.

- `app ls` — list entries under `/apps` on the mounted NeeleFS v2 volume
- `app run <name>` — run a known built-in application (e.g. `keymusic`)
- `app run </path>` — read a NeeleFS text descriptor and run by name; descriptors may start with `MEZCMD\n` followed by the app name

Bundled apps
- `keymusic` — tone generator controlled by keyboard (C D E F G A H/B, Ctrl+Q to exit)

Applications obtain services (console, keyboard, timers, PC speaker, text mode) via `mez_api_get()`; see `docs/api/mezapi.md`.
