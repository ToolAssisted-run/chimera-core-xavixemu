# AGENTS.md - XaviXEmu core for Chimera

This repository builds XaviXEmu (an emulator of XaviX TV games) as a sandboxed
guest for Chimera, a frontend for tool-assisted speedruns. It produces one
file, `xavixemu.chimeraCore`: a zip holding `core.wbx` (the emulator built for
the miniBox sandbox), `waterbox.config`, `file_slots.json`,
`default_keybinds.json`, `languages/`, `build.json` and the licence texts.
`docs/BUILDING.md` has the detailed build; this file is the short operating
guide.

## Layout

- `extern/XaviXEmu/` - upstream, a git submodule pinned to one commit. Only
  `src/core`, `src/sha1.c` and `third_party/miniz` are built. The two patches
  in `patches/` are laid over it by `waterbox/apply-patches.sh` (meson and
  `build-package.sh` run it). To change a patch: edit the tree, run
  `waterbox/make-patches.sh`, and `apply-patches.sh` must say "already applied".
- `meson.build`, `meson_options.txt` - one build description, two flavors:
  native (`run-native`, `run-wbx`, and upstream's own `test-*` programs) and
  guest (`core.wbx`).
- `waterbox/cinterface.c` - the adapter: XaviXEmu behind Chimera's guest ABI.
  Compiled unchanged into both flavors. `waterbox/games.inc` is the table of
  ROM sets, written from upstream's `src/rom_loader.c` (size, CRC-32, SHA-1;
  each set's game name and the reason it is played or refused).
- `waterbox/native-shim/emulibc.h` - replaces miniBox's emulibc in the native
  build.
- `waterbox/run-native.c`, `waterbox/run-wbx.c`, `waterbox/gate-harness.h` -
  the reference driver, the sandbox driver, and the run-and-digest loop they
  share.
- `waterbox/waterbox.config`, `file_slots.json`, `default_keybinds.json`,
  `package-licenses.json`, `languages/*.json` - what Chimera is told.
- `waterbox/setup-guest.sh`, `waterbox/build-package.sh` - the build scripts.
- `waterbox/run-gate.sh` - the core tests. `waterbox/tests/run-frontend.sh`
  and its helpers - the frontend tests.
- `tests/route.py` (the pad route the tests play), `tests/forge.py` (makes
  files with a ROM set's size and CRC-32 that are no game, to test the
  refusals), `tests/same-picture.py`, `tests/upstream-harness.c` (upstream's
  sources driven by a program with none of this core's code).
- `docs/PLAN.md` - the design log. `docs/BUILDING.md` - the build.
- `.github/workflows/chimera.yml` - CI and publishing.
- `build/` - every output (gitignored).

## Commands, in order

```
git submodule update --init
MB=<chimera>/extern/chimera-common-minibox
meson setup "$MB/build/meson-linux" "$MB" && meson compile -C "$MB/build/meson-linux"
meson setup build/meson-native -Dminibox_dir="$MB" && ninja -C build/meson-native
./waterbox/build-package.sh -m "$MB" -r <chimera>        # also builds core.wbx
./waterbox/run-gate.sh                                     # no game: the checks that need none pass, the rest SKIP
XAVIXEMU_ROM_DIR=<folder holding epo_ebox.zip> CHIMERA_ROOT=<chimera> ./waterbox/run-gate.sh
XAVIXEMU_ROM_DIR=<folder> env -u DISPLAY QT_QPA_PLATFORM=offscreen ./waterbox/tests/run-frontend.sh --chimera-root <chimera>
<chimera>/build/meson-linux/source/ui/test-packages <folder holding only xavixemu.chimeraCore>
```

Rebuild both flavors before testing (`ninja -C build/meson-native && ninja -C
build/meson-guest`): the tests build nothing and compare what is there.

## Rules of this repository

- Upstream stays at its pinned commit. A change to it is a numbered patch in
  `patches/`, a build option or a hook, never a deletion.
- Determinism is the product. The guest reads no host time, no randomness,
  nothing that differs between runs, and a savestate must round-trip.
- A new check is broken on purpose once and seen to fail; say so in the
  commit (docs/PLAN.md lists the ones done).
- Never commit a game. `tests/roms-local/` is gitignored for local files.
  A refusal is tested with files of `tests/forge.py`, which are no game.
- Only games whose controls are a plain pad are played. A game that needs more
  is refused by name until a declaration for it exists; do not special-case
  one in Chimera.
- Every text a person reads is a key of `waterbox/languages/*.json`, in all
  26 languages, in the same commit.
- Do not write `version` or `versionDate` into `waterbox.config`:
  `build-package.sh` stamps them.
- Documentation is plain ASCII and plain language.
- Commits: `type(scope): what changed`, a short body with what was tested and
  the numbers. Stage explicit paths.
