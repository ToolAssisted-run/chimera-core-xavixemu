# chimera-core-xavixemu

[XaviXEmu](https://github.com/BillyJr0127/XaviXEmu) as a
[Chimera](https://github.com/ToolAssisted-run/chimera) core: XaviX plug-and-play
games, in miniBox's sandbox, packaged as `xavixemu.chimeraCore`.

XaviX is the chip inside a family of TV games (Epoch's Excite series, Takara
Tomy, Bandai's Let's! TV Play and others) that are played with a controller
that senses the player: a camera, a motion sensor, a steering wheel. XaviXEmu
is an emulator of them in portable C. This repository builds its emulation
sources behind Chimera's interface.

## What it plays

Only the games whose controls are plain: a pad of four directions and two
buttons that the machine reads as bits of one register. XaviXEmu recognises 28
ROM sets in the version this core builds, and this core accepts three of them:

| Set | Game | Tried |
|---|---|---|
| `epo_ebox` | Excite Boxing (Japan) | yes, see docs/PLAN.md |
| `ttv_mx` | MX Dirt Rebel | no: no copy was on hand |
| `tom_jump` | IDATEN Jump: Gekisou IDATEN Battle (Japan) | no: no copy was on hand |

The other 25 sets are recognised and refused, with a message that names the
game and says why: a camera or motion sensor, analog sticks or a wheel, a
keyboard and mouse, a card scanner, or the XaviX 2 machine, which XaviXEmu
marks as experimental and this core does not run. Games that sense motion
may come later.

## What it is

- **One game a project**: the ZIP of ROM chips as MAME names the set (for
  example `epo_ebox.zip`), or the bare ROM image. Only the exact images
  XaviXEmu knows are accepted (size, CRC-32 and SHA-1).
- **The controls**: Up, Down, Left, Right, OK, Back, and Pause (wired in the
  racing games only).
- **Picture and sound**: 256 by 224 at 60 frames a second, 48000 Hz stereo.
- **Save data**: a game remembers in 4 KiB of battery-backed memory or in a
  1 KiB serial EEPROM. A savestate carries it. Emulator > Export Save Data...
  writes it as `<set>.sav` (for example `epo_ebox.sav`), and the project's save
  data slot takes that file back.
- **Memory**: main RAM, fragment RAM, sound RAM and the ROM; the CPU's registers.

## What you bring

The game, as above. No game is included, and none may be sent to this
repository.

## Using it in Chimera

Chimera ships no cores and downloads none. Download the package from this
repository's Releases page, or build it, and put it in the `cores` folder
beside Chimera's program. Tools > Core Manager... lists the cores there.

## What is not proved

One game was run: Excite Boxing. The other two games were not tried. The
package was built and tested on x86_64 Linux only: Windows, macOS and
aarch64 were not tried. docs/PLAN.md says what each test compares.

## Building and testing

`docs/BUILDING.md` has every step, from a fresh clone to the tests. In short,
with `<chimera>` a Chimera checkout and `<minibox>` its
`extern/chimera-common-minibox`:

```
git submodule update --init
meson setup build/meson-native -Dminibox_dir=<minibox> && ninja -C build/meson-native
./waterbox/build-package.sh -m <minibox> -r <chimera>
./waterbox/run-gate.sh
./waterbox/tests/run-frontend.sh --chimera-root <chimera>
```

The tests that play a game need Excite Boxing (`XAVIXEMU_ROM_DIR`, or
`tests/roms-local`) and say SKIP without it. An AI coding agent should start
with [AGENTS.md](AGENTS.md). The design log is [docs/PLAN.md](docs/PLAN.md).

## Licence

This repository is MIT. XaviXEmu is BSD 3-Clause, and its miniz library is
MIT. See [LICENSE](LICENSE).
