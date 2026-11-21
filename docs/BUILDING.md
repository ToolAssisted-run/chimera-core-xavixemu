# Building the XaviXEmu core

This repository builds XaviXEmu's emulation sources as a sandboxed guest
(`core.wbx`) and packages it as one file, `xavixemu.chimeraCore`, which Chimera
loads as XaviX. The steps below are the ones `.github/workflows/chimera.yml`
runs from a fresh clone on a public Ubuntu runner (x86_64 only; see
"Platforms").

Two placeholders are used throughout:

- `<chimera>` is the absolute path of a Chimera checkout
  (https://github.com/ToolAssisted-run/chimera).
- `<minibox>` is `<chimera>/extern/chimera-common-minibox`, the miniBox
  submodule of that checkout. miniBox is the sandbox host and the guest
  toolchain.

## Requirements

- Linux on x86_64. CI uses `ubuntu-24.04`.
- git.
- For the core, the package and the core tests:

  ```
  sudo apt-get update
  sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3
  ```

  The compiler is the distribution's gcc. The guest is compiled by the same
  gcc through miniBox's musl specs file, so there is no cross compiler to
  install. XaviXEmu is plain C: no C++ guest toolchain, and nothing is
  downloaded.
- For the frontend tests only, Chimera built with its window. Chimera's
  `tools/ci-chimera.sh` installs what that needs (Qt 6.12) and builds it.

## Get the sources

```
git clone --recursive https://github.com/ToolAssisted-run/chimera-core-xavixemu.git
cd chimera-core-xavixemu
```

The submodule is `extern/XaviXEmu`, pinned to one upstream commit. Two small
patches in `patches/` are laid over it (next section). Upstream's `main.c`
and the Windows files around it are not built, and neither is `src/xavix2`.

A Chimera checkout, for miniBox:

```
git clone https://github.com/ToolAssisted-run/chimera.git <chimera>
git -C <chimera> submodule update --init extern/chimera-common-minibox
```

The frontend tests build Chimera itself and need all of its submodules
(`git -C <chimera> submodule update --init --recursive`).

## The patches

Two, in `patches/`, both hooks that change nothing about the machine:

1. `0001-input-read-hook.patch`: a function pointer, called each time the
   program reads the first button register. The core uses it to say which
   frames did not read the pad (lag frames), and to call Chimera's input poll.
2. `0002-skip-drawing.patch`: `drgqst_core_run_frame` returns without drawing
   when the frontend says nobody will see the frame (turbo). The picture is
   derived from the machine and feeds nothing back; the tests check this.

`waterbox/apply-patches.sh` lays them over `extern/XaviXEmu`. It leaves a tree
that already has them alone, and refuses a tree that is partly patched
(it says how to start again). Meson and `build-package.sh` run it; run it by
hand once after cloning if you want to read the patched sources. After
editing the patched tree, `waterbox/make-patches.sh` writes the patches again.

## Build miniBox

```
meson setup <minibox>/build/meson-linux <minibox>
meson compile -C <minibox>/build/meson-linux
```

This builds the host library the sandbox driver links and the C guest
toolchain.

## The native reference and the sandbox driver

```
meson setup build/meson-native -Dminibox_dir=<minibox>
ninja -C build/meson-native
```

`build/meson-native` holds `run-native` (the same `cinterface.c` and XaviXEmu
sources built for the host, with a shim in place of miniBox's emulibc),
`run-wbx` (drives `core.wbx` through the miniBox host the way Chimera does),
and upstream's own tests as `test-sha1`, `test-machine`, `test-cpu`,
`test-video`, `test-peripherals`, `test-audio` and `test-state`.

## The package

```
./waterbox/build-package.sh -m <minibox> -r <chimera>
```

This lays the patches, configures and builds the guest (`core.wbx`, through
`waterbox/setup-guest.sh` and a Meson cross file it writes), checks the image
(`check-wbx.sh`), adds the licence texts and miniBox's ready-made Export Save
Data and CPU Registers windows, stamps the version, and writes
`<chimera>/build/Cores/xavixemu.chimeraCore`. The version is the commit
(`CORE_VERSION` in CI; `<commit>+local` by hand), with the date of that commit.
Packaging is deterministic: the same commit gives the same file.

## The tests

```
./waterbox/run-gate.sh
```

With no game, every check that needs none runs (the patches, upstream's seven
test programs, the table of ROM sets, and nine refusals) and the checks that
play a game say SKIP. With Excite Boxing, a ZIP of the set `epo_ebox`
(4194304 bytes inside, CRC-32 e25ae4f5):

```
XAVIXEMU_ROM_DIR=<folder holding epo_ebox.zip> CHIMERA_ROOT=<chimera> ./waterbox/run-gate.sh
```

The second variable adds the checks through `chimera-run` (a project of the
game and the route, played by Chimera's own runner). `<chimera>` needs
`build/meson-linux/chimera-run` and `build/Cores/xavixemu.chimeraCore`.

The frontend tests, Chimera's window run headless:

```
XAVIXEMU_ROM_DIR=<folder> env -u DISPLAY QT_QPA_PLATFORM=offscreen \
    ./waterbox/tests/run-frontend.sh --chimera-root <chimera>
```

And Chimera's contract tests, which need no game:

```
<chimera>/build/meson-linux/source/ui/test-packages <folder holding only xavixemu.chimeraCore>
```

`docs/PLAN.md` lists every check and what it compares.

## Platforms

The package is built on x86_64 Linux and tested there. The guest is one file
for every x86_64 system, and `waterbox.config` says it runs on Windows, Linux
and macOS of that CPU. Only Linux was tried. An aarch64 package would be a build
of its own on an aarch64 machine (miniBox's rule) and is not built.

## CI

`.github/workflows/chimera.yml` has a core gate (build, native == sandbox,
package), a frontend gate (Chimera built, the package through the window, the
core tests again through `chimera-run`, the contract tests) and, on a schedule
or by hand, a publish job that uploads `xavixemu-<sha>` and calls Chimera's
reusable publishing workflow. CI has no game, so the checks that play one say
SKIP there. Start a first release by hand: `gh workflow run chimera.yml`.

## Troubleshooting

- `the series does not apply`: the submodule moved without the patches being
  made again for it.
- `extern/XaviXEmu is not checked out`: `git submodule update --init`.
- `Init failed: error.not_plain ...` from a runner: the file is a game this
  core does not play; the key after it says why.
- A check that says SKIP did not run. It is never a pass.
