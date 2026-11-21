# Design log: the XaviXEmu core

Dated entries, newest last. Chimera issue 631 asked for this core; the owner
decided on 2026-11-21 to build it for the XaviX games with plain controls,
declaring a digital pad, and to leave the games that sense motion for later.

## 2026-11-21 - what was built, and what was proved

A core for XaviXEmu (https://github.com/BillyJr0127/XaviXEmu, C, BSD 3-Clause),
pinned at commit 650916814fc2 ("docs: publish 0.4.1 binary downloads and
Traditional Chinese guide", 2026-09-27).

**One game was run, Excite Boxing (Japan).** Its ZIP holds one file,
`exciteboxing.bin`, 4 MiB, CRC-32 e25ae4f5, set name `epo_ebox`. What the tests
show about it (numbers are in "The tests"): the sandboxed core and the same
sources built for the host give the same picture, sound, lag frames, memory and
whole state image over 1800 frames of a scripted route; a save and a load
before every frame change nothing; a state carried into a new process ends
where the straight run does; and the picture is, pixel for pixel, the one
XaviXEmu's own unpatched sources show when driven by their own program.

**Not proved:** the other two games the core accepts (`ttv_mx`, `tom_jump`: no
copy was on hand), any game beyond the first minutes of Excite Boxing's route
(the route reaches menus and the first screens; nobody played a match), Windows,
macOS, aarch64, and a real GPU (there is none: the picture is the machine's own
framebuffer).

### What upstream is, and a finding about its version

XaviXEmu's emulation is plain C in `src/core` (a 65C02-family CPU with the
XaviX extensions, video, audio, a serial EEPROM, and `drgqst_core.c`, which
joins them and runs one frame). It has no operating-system calls. Its Windows
program is `src/main.c`, 4300 lines, which also holds every game's control
mapping and is not used here.

The newest commit's README and docs describe release 0.4.1 and list 40 ROM
sets. Its source is release 0.3.1: `src/rom_loader.c` recognises 28 sets (and
the two-chip set of Ham-chans, which it assembles), 29 in all. The 11 sets that
the documents list and the source does not (`rad_mtrk`, `rad_snow`, `rad_ssx`,
`rad_sbw`, `tak_gin`, `tcarnavi`, `tomthr`, `epo_crok`, `tak_zuba`, `epo_golf`
and `duelmast`) are in the binaries upstream publishes and not in the source. This core builds the source. A newer commit that publishes the 0.4.1
source would add sets; the table is then written again from it.

### The feasibility answers (docs/new-core.md, section 1)

- **A new core at all?** Yes: no core here runs XaviX.
- **Licence:** BSD 3-Clause for XaviXEmu; MIT for its bundled miniz. XaviXEmu says
  its hardware behaviour was adapted from MAME files marked BSD-3-Clause and
  lists them (THIRD_PARTY_NOTICES.md), so the package carries MAME's name and
  commit as a component. Modified and binary redistribution is allowed.
- **One thread on one clock:** yes. One function runs a frame of
  21477272 / 60 clock ticks (the remainder carried into the next frame), with
  no thread and no host time.
- **Where the picture comes from:** the machine's own framebuffer, 256 by 224.
- **A recompiler:** none; a plain interpreter.
- **What the user brings:** the game. No firmware: XaviX games are one ROM.
- **What a test can run on a public runner:** upstream's own unit tests, the
  table and its refusals (with files made by `tests/forge.py`, below), the
  patches and the package. The tests that play need a game and say SKIP.
- **How big:** the machine is 372576 bytes of data; the ROM is up to 8 MiB.

### The games: which are played, and why

Chimera's declaration for a game's controls is a list of buttons. A game that
needs more than buttons needs a declaration Chimera does not have (a camera
picture, a motion sensor's readings, an analog axis it can bind), so it is
refused by name rather than played with a stand-in that the original never had.
`waterbox/games.inc` has all 28 sets, each with a reason:

| Reason | Sets |
|---|---|
| none (played) | `epo_ebox`, `ttv_mx`, `tom_jump` |
| camera or motion sensor | `drgqst`, `ban_onep`, `ban_omt`, `ttv_lotr`, `ttv_sw`, `ttv_swj`, `epo_bowl`, `epo_hamc`, `tom_dpgm`, `epo_mini`, and the two-chip `epo_hamd` |
| analog sticks or a wheel | `epo_sdb` (four analog channels), `tak_chq` (a wheel) |
| a keyboard and mouse | `tvpc_dor`, `tvpc_ham`, `tvpc_hk` |
| a card scanner | `epo_es2j` (its controls are plain, but upstream says the scanner's protocol is unverified) |
| the XaviX 2 machine | `ban_naru`, `ban_bldj`, `ban_db2j`, `ban_dbz`, `epo_dab2j`, `epo_dtcj`, `epo_pabj`, `epo_ssk2`, `epo_sskj` |

How the three played games were chosen. XaviXEmu's `main.c` has a function,
`rom_uses_digital_direction_input`, that names the sets whose controller it
reads as bits of the first button register: `ttv_mx`, `tom_jump` and
`epo_ebox`. Its comment for the machine profile of the first two says they
"use plain digital controls on IN0". For Excite Boxing it says "only digital
P0 controls". Those are the three. In upstream's program the bits are driven
from the mouse position and the keys; the core drives them from the pad.

**Not tried:** `ttv_mx` and `tom_jump`. They are accepted because upstream's own
source says their controls are the same bits, and they use the same machine
code path with a serial EEPROM in place of the battery-backed memory. A person
with a copy should run them before saying they work.

**The XaviX 2 family is left out.** `src/xavix2` is a different CPU and machine
(a 32-bit core with a polygon GPU), which upstream marks experimental. Of its
nine sets, four are driven by optical or motion packets, and two upstream
lists as not working. Nothing about it works "the same way" as the plain
games: it has its own machine and its own input. A core for it would be a
second machine in the package and its own piece of work.

## 2026-11-21 - decisions

- **The game's file.** The slot takes a ZIP of ROM chips as MAME names a set
  (the form XaviXEmu itself takes) or the bare image (`.bin`). The core finds
  the member by its size and CRC-32, then checks the SHA-1 (upstream's
  `src/sha1.c`), as `rom_loader.c` does. A file that is not a known set is
  refused with its name; a set that is known but refused says why; an image
  with a known size and CRC-32 and another SHA-1 says so.
- **The ROM is in ordinary memory.** Init runs before the sandbox is sealed, so
  the ROM is part of the sealed baseline and no savestate carries it. A state
  is the sandbox's snapshot of the pages that changed since then; the whole
  machine (`drgqst_core`) is 372576 bytes.
- **Savestates are the sandbox's** (arena snapshots), not XaviXEmu's
  `drgqst_state_save`. The state image upstream writes (22539 bytes, a fixed
  little-endian layout) is used by the tests as a second witness: an export,
  `XavixStateHash`, hashes it, and the tests compare that hash native against
  sandbox every 60 frames. It is not a memory domain, because a domain read
  would have to serialise the machine on every read.
- **Two patches** (patches/), both hooks:
  1. a function pointer called when the program reads the pad's register: the
     lag frames, and Chimera's input-poll callback;
  2. a global that makes `drgqst_core_run_frame` skip drawing, for turbo.
  Upstream's `render_frame` is the last thing a frame does, writes only the
  picture, and the picture is not in the state image; the tests check that a
  run with half its frames undrawn ends in the same machine.
- **The pad.** `Up Down Left Right OK Back Pause`, in that order (the wire
  format). They are bits `$10 $20 $40 $80 $01 $02 $04` of the first button
  register, active high, as upstream's front end sets them. Two opposite
  directions cancel, as upstream's front end has it. Pause is wired in the two
  tilt-and-race games and left open in Excite Boxing ("intentionally unbound"
  upstream); the core clears it there. Excite Boxing's real controller senses
  motion; upstream maps its mouse and keys to the same four direction bits, and
  so does this core.
- **Save data.** The machine remembers either in 4 KiB of battery-backed
  memory inside its main RAM (Excite Boxing) or in a 1 KiB serial EEPROM
  (`ttv_mx`, `tom_jump`). Both are in a savestate. Export Save Data writes the
  one the game has as `<set>.sav`; the save data slot takes it back and refuses
  another name or size. The name has the set in it so that one game's file is
  not taken for another's.
- **No settings, no clock, no firmware.** The machine has no calendar clock
  and no settings that shape it; there is nothing for a project to pin.
- **Sound** is 48000 Hz stereo, 800 pairs a frame; the core reports the count.
  **Picture** is 256 by 224 at exactly 60 frames a second.
- **Memory layout** 4/4/4/4/40 MiB (sbrk, sealed, invisible, plain, mmap): the
  mmap heap holds the ROM (up to 8 MiB) and, while the game is read, the ZIP
  (up to 16 MiB) and its image.
- **Platforms.** `runsOn` lists Windows, Linux and macOS on x86_64. Only
  Linux was tried. No aarch64 package is built (the rule is a build on an
  aarch64 machine); CI uploads `xavixemu-<sha>` with no `arches`.
- **Translations.** Chimera has 26 languages; the core's texts are in all of
  them. The ones other than English and German were written without a native
  reader and are for a speaker to look over.

## The tests

`waterbox/run-gate.sh`; `CHIMERA_ROOT` adds the checks through `chimera-run`.
"SKIP" is said where a game is needed and not on hand.

Results on 2026-11-21 (x86_64 Linux, an Ubuntu 24.04 container, with Excite
Boxing): the gate 26 ok, 0 failed; without the game 12 ok, 0 failed, 14 SKIP;
the frontend tests 2 ok; Chimera's contract tests 0 failed. The route is 1800
frames: 400 idle, then turns of 240 frames pressing OK, a direction and OK or
Back (`tests/route.py`), the route of the first headless run of upstream's
sources.

| Check | What it compares | Needs the game |
|---|---|---|
| patches | the series applies and the submodule is at the pinned commit | no |
| upstream-tests | XaviXEmu's 7 own test programs pass on the patched sources | no |
| refuse:* (9) | a file with a set's size and CRC-32 (made by `tests/forge.py`, no game) gives the refusal and the reason named, in native and sandbox alike: a camera game bare and zipped, a 2 MiB one, one chip of the two-chip set, a XaviX 2 game, an Excite Boxing-sized file with wrong contents bare and zipped, noise, a broken ZIP | no |
| games-table | 28 sets, 3 played | no |
| eb:equivalence | native against sandbox: picture, sound, lag count, all 4 memory domains, and the machine's whole state image every 60 frames | yes |
| eb:input-shaped | the route's picture and state differ from 1800 idle frames | yes |
| eb:lag | 2 of 1800 frames did not read the pad | yes |
| eb:sound | 1440000 sample pairs (800 a frame), 1706937 values not silent | yes |
| eb:savestate | a save and a load before every frame change nothing | yes |
| eb:state-new-process | a state at frame 900 loaded into a new sandbox ends where the straight run does | yes |
| eb:turbo | half the frames undrawn: same machine, sound and later pictures | yes |
| eb:upstream-picture | the picture after 1800 frames equals, pixel for pixel, that of upstream's unpatched sources run by `tests/upstream-harness.c` | yes |
| savedata:export / import / refused | the 4096-byte file is the machine's battery-backed memory, native == sandbox; a file put in the slot is that memory when the machine is made; another name or size is refused | yes |
| engine:project | chimera-run plays a project of the game and the route: main RAM and the last picture are the native reference's | yes |
| engine:input-polls | 1811 reads of the pad reach Chimera in 1800 frames; counting moves nothing | yes |
| engine:rewind | with a state kept every 300 frames, and with a rewind to frame 1000, the run ends in the same main RAM | yes |
| frontend (run-frontend.sh) | the window plays the route: main RAM equals the native reference's; the package's keybinds are the window's | yes |

Speed, on a machine at a load average of about 240 (so slow): native 600
frames in 4.7 s; the sandbox about the same (5.2 s); chimera-run 1800 frames
in 14 s.

**Each new check was broken on purpose once and seen to fail** (2026-11-21),
by editing a copy of the repository: the pad read not noticed (eb:lag failed);
the exported memory one byte late (savedata:export, import); the name check
removed (savedata:refused); a camera game made playable (refuse:motion-bin,
refuse:motion-zip, games-table); XaviX 2 given the camera reason
(refuse:xavix2); a broken ZIP called unknown (refuse:broken-zip); turbo
skipping nothing (eb:turbo); a sample pair too few (eb:sound); the native
build alone nudging the CPU (eb:equivalence); the pad ignored (eb:input-shaped,
eb:upstream-picture); the driver loading the state of the frame before
(eb:savestate); Chimera not told of the pad reads (engine:input-polls); a
counter outside the savestate written into the machine (eb:state-new-process,
engine:rewind); a patched file touched (patches); a failing upstream test
(upstream-tests); a default binding changed (keybinds); the native run played
without the route (game:frontend). Not broken: engine:project (the
nudge above left main RAM and the picture alone, so it passed; its subject is
the same as eb:equivalence's through another path).

## Questions for the owner

- The three games the core accepts: is it right to accept `ttv_mx` and
  `tom_jump` before anyone has run them?
- Should the source of a newer XaviXEmu (0.4.1) be waited for, or
  asked of its author (11 more sets, some with plain controls by their notes), before this is published?
- The roster row and its public repository are the owner's to make.
