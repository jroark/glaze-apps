# CLAUDE.md

Installable apps for Glaze (the Casio BE-300 OS in the sibling `../glaze-os`
repo), built against its application SDK. See README.md for the app list and commands.

- **Build:** `./build.sh` (Docker, `mips-cross-dev`, mounting `../glaze-os`) or `make`
  with `mipsel-linux-gnu-gcc`. Run `make sdk` in glaze-os first; `GLAZE_SDK` and
  `GLAZE_DIR` override the sibling paths. Builds must stay warning-free.
- **Test:** `make test` runs the native tests (`tests/Makefile`, run from `tests/`;
  app paths are `../`-relative and glaze-os sources come from `$(GLAZE)`).
  Scripted UI tests (`tests/ui/*.txt`) run through `../glaze-os/tests/run.sh`.
- **Layout:** `packages/<app>` is an SDK project (sources, `Info`, `icon.gzi`, `res/`,
  per-app `Makefile` with its flags). Apps keep their data in `<bundle>/data`.
  Glaze's public headers (`glaze/*.h`) from the SDK are the only OS interface.
- **Platform:** freestanding MIPS II, soft float, Glaze's minimal libc, libgfx and
  libui. Never link libgcc or a Linux libc. Follow the OS's app lifecycle: save and
  release shared resources on `GZ_EV_APP_DEACTIVATE`, since a suspended app may be ended.
- **Copies of glaze-os code:** `packages/chatgpt/http/` holds byte-identical copies of
  the Browser's HTTP/TLS transport. Fix bugs in glaze-os, then re-sync (see its README).
- **AIM** speaks OSCAR to a server the user names (AOL's are gone). `tests/native/aim.c`
  takes `--live host port sn pw [buddy msg]`. **Gmail** speaks IMAPS/SMTPS; its test
  binary runs a local mock server with `--serve <imap_port> <smtp_port>` and checks a
  real one with `--live`. **Doom** packs no WAD and fetches none; engine pins and
  patches are in `third_party/doomgeneric/UPSTREAM.md`. **Chat** signs in with
  `tools/chatgpt_signin.py`; see `docs/CHATGPT.md`.
- **Never commit** credentials or `CODEX.AUTH` exports, App Passwords, or WADs.
