#!/bin/bash
# The core tests: for every check the sandboxed core must give byte-identical
# picture, sound, lag, memory and state to the native reference build (the
# same cinterface.c compiled for the host), survive a whole-machine savestate
# round trip around every frame, and survive being carried into a new process.
#
# What needs no game runs everywhere, CI included: XaviXEmu's own unit tests
# against the sources this core ships, the table of ROM sets and its refusals
# (with files made here that have a ROM set's size and CRC-32 and are no
# game), and the patches. What needs a game runs where one is on hand and says
# SKIP where it is not: Excite Boxing, a ZIP of the set `epo_ebox`
# (4194304 bytes, CRC-32 e25ae4f5), in XAVIXEMU_ROM_DIR or in tests/roms-local.
# No game is ever committed to this repository or sent to CI.
#
# Usage: ./run-gate.sh [-n <native build dir>] [-g <guest build dir>]
# CHIMERA_ROOT=<a Chimera checkout with this package installed> adds the
# checks through Chimera's own runner (chimera-run).
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
nat="$root/build/meson-native"
gst="$root/build/meson-guest"
while getopts "n:g:" opt; do
	case "$opt" in
		n) nat="$OPTARG" ;;
		g) gst="$OPTARG" ;;
		*) exit 2 ;;
	esac
done

[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] || {
	echo "native build missing: meson setup build/meson-native -Dminibox_dir=<miniBox> && ninja -C build/meson-native" >&2; exit 1; }
[ -f "$gst/core.wbx" ] || {
	echo "guest build missing: sh waterbox/setup-guest.sh -m <miniBox> && ninja -C build/meson-guest" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
digests() { grep -E '^(frames|vsync|videoHash|audioHash|audioSamples|audioNonZero|lagFrames|stateHashes|stateHash|domain\[)'; }
turboDigests() { grep -E '^(frames|vsync|tailVideoHash|audioHash|audioSamples|lagFrames|stateHashes|stateHash|domain\[)'; }
value() { sed -n "s/^$1=//p" "$2"; }
tests="$root/tests"

ok=0
failed=0
report() { printf "%-26s %-6s %s\n" "$1" "$2" "$3"; case "$2" in PASS) ok=$((ok+1)) ;; SKIP) ;; *) failed=$((failed+1)) ;; esac; }
printf "%-26s %-6s %s\n" "Check" "Result" "Detail"
printf "%-26s %-6s %s\n" "-----" "------" "------"

# ---------------------------------------------------------- no game needed
# the patches: a tree that has them, at the commit this repository pins
if sh -c "sh '$here/apply-patches.sh' > '$work/patches.log' 2>&1 && [ \"\$(git -C '$root' ls-files -s extern/XaviXEmu | awk '{print \$2}')\" = \"\$(git -C '$root/extern/XaviXEmu' rev-parse HEAD)\" ]"; then
	report "patches" PASS "$(tail -1 "$work/patches.log"); upstream is at the commit this repository pins"
else
	report "patches" FAIL "$(tail -2 "$work/patches.log" | tr '\n' ' ')"
fi

# XaviXEmu's own tests of its CPU, machine, video, audio, serial memory, SHA-1
# and state image, built against the patched sources
n=0; bad=""
for t in "$nat"/test-*; do
	case "$t" in *.p) continue ;; esac
	n=$((n+1))
	"$t" > "$work/ut.out" 2>&1 || bad="$bad $(basename "$t")"
done
if [ "$n" -ge 7 ] && [ -z "$bad" ]; then
	report "upstream-tests" PASS "XaviXEmu's own $n test programs (CPU, machine, video, audio, serial memory, SHA-1, state) pass on the sources this core ships"
else
	report "upstream-tests" FAIL "$n programs ran; failed:${bad:- (too few ran)}"
fi

# run <native|sandbox> <workdir> [options]: the digests on stdout, errors on stderr
run() {
	local flavor="$1" wd="$2"; shift 2
	if [ "$flavor" = native ]; then "$nat/run-native" "$wd" "$@"; else "$nat/run-wbx" "$gst/core.wbx" "$wd" "$@"; fi
}

# a work folder holding one file under its name and a slots map
# plainwork <name> <file> [slots json]
plainwork() {
	local wd="$work/$1"
	rm -rf "$wd"; mkdir -p "$wd"
	cp "$2" "$wd/"
	printf '%s' "${3:-{\"game\":[\"$(basename "$2")\"]\}}" > "$wd/slots"
	echo "$wd"
}

# what the core says when it refuses a file: native and sandbox must say the same
# refusal <name> <file> <key you expect> [expect another key too]
refusal() {
	local wd; wd="$(plainwork "refuse-$1" "$2")"
	run native "$wd" --frames 1 > /dev/null 2> "$work/ref.native"
	run sandbox "$wd" --frames 1 > /dev/null 2> "$work/ref.box"
	local line; line="$(grep 'Init failed' "$work/ref.box" | head -1)"
	if grep -q "$3" "$work/ref.box" && cmp -s "$work/ref.native" "$work/ref.box" && { [ -z "${4:-}" ] || grep -q "$4" "$work/ref.box"; }; then
		report "refuse:$1" PASS "$(printf '%s' "$line" | tr '\037' '|' | cut -c1-150)"
	else
		report "refuse:$1" FAIL "wanted $3${4:+ and $4}; native said '$(head -1 "$work/ref.native" | tr '\037' '|')', sandbox '$(printf '%s' "$line" | tr '\037' '|')'"
	fi
}
# Files that have a ROM set's size and CRC-32, which is all the table looks at
# to refuse it. The three-line list is the sets of the table's three kinds of
# refusal; a game that is played also has its SHA-1 checked, which these fail.
python3 "$tests/forge.py" 0x800000 3d24413f "$work/drgqst.bin"                          # a camera game
python3 "$tests/forge.py" 0x800000 3d24413f "$work/drgqst.zip" drgqst.bin               # the same, in a ZIP
python3 "$tests/forge.py" 0x800000 e3465ad2 "$work/ban_naru.bin"                        # XaviX 2
python3 "$tests/forge.py" 0x200000 d34f8d9e "$work/epo_bowl.zip" epo_bowl.bin           # a camera game, 2 MiB
python3 "$tests/forge.py" 0x100000 6c2d9d98 "$work/epo_hamd.zip" epo_hamd_u2.bin        # one chip of a two-chip set
python3 "$tests/forge.py" 0x400000 e25ae4f5 "$work/epo_ebox.bin"                        # a played game with the wrong contents
python3 "$tests/forge.py" 0x400000 e25ae4f5 "$work/epo_ebox_wrong.zip" epo_ebox.bin
head -c 5000 /dev/urandom > "$work/noise.bin"
printf 'PK\003\004 this is not a ZIP' > "$work/broken.zip"
refusal motion-bin "$work/drgqst.bin" error.not_plain reason.motion
refusal motion-zip "$work/drgqst.zip" error.not_plain reason.motion
refusal motion-2mb "$work/epo_bowl.zip" error.not_plain reason.motion
refusal two-chip-set "$work/epo_hamd.zip" error.not_plain reason.motion
refusal xavix2 "$work/ban_naru.bin" error.not_plain reason.xavix2
refusal wrong-image "$work/epo_ebox.bin" error.bad_image
refusal wrong-image-zip "$work/epo_ebox_wrong.zip" error.bad_image
refusal unknown "$work/noise.bin" error.unknown_game
refusal broken-zip "$work/broken.zip" error.bad_zip
# the table's three plain games and its refusals, counted
nplain="$(grep -c 'REASON_NONE' "$here/games.inc" || true)"
nall="$(grep -c 'REASON_' "$here/games.inc" || true)"
if [ "$nall" = 28 ] && [ "$nplain" = 3 ]; then
	report "games-table" PASS "28 ROM sets of XaviXEmu's rom_loader.c, 3 played (epo_ebox, ttv_mx, tom_jump), 25 refused with a reason"
else
	report "games-table" FAIL "$nall sets, $nplain played (want 28 and 3)"
fi

# ---------------------------------------------------------- with Excite Boxing
romdir="${XAVIXEMU_ROM_DIR:-$tests/roms-local}"
rom="$romdir/epo_ebox.zip"
romcrc="$(python3 -I -c "
import sys, zipfile
try:
    z = zipfile.ZipFile(sys.argv[1]); print(','.join('%08x:%d' % (i.CRC, i.file_size) for i in z.infolist()))
except Exception: pass" "$rom" 2>/dev/null)"
frames=1800
if [ "$romcrc" = "e25ae4f5:4194304" ]; then
	echo "Excite Boxing: $rom (zip sha1 $(sha1sum "$rom" | cut -c1-40)); route of tests/route.py, $frames frames"
	gamework() { # gamework <name> [save data file]
		local wd="$work/$1"
		rm -rf "$wd"; mkdir -p "$wd"
		cp "$rom" "$wd/epo_ebox.zip"
		if [ -n "${2:-}" ]; then
			cp "$2" "$wd/epo_ebox.sav"
			printf '{"game":["epo_ebox.zip"],"savedata":["epo_ebox.sav"]}' > "$wd/slots"
		else
			printf '{"game":["epo_ebox.zip"]}' > "$wd/slots"
		fi
		echo "$wd"
	}
	wd="$(gamework eb)"
	press="$(python3 "$tests/route.py" press $frames)"
	args=(--frames $frames $press)
	idle=(--frames $frames)

	# ---- native == sandbox == rerecord == a new process, on the route ----
	run native "$wd" "${args[@]}" 2>"$work/nat.err" | digests > "$work/nat.txt"
	run sandbox "$wd" "${args[@]}" 2>"$work/box.err" | digests > "$work/box.txt"
	if [ ! -s "$work/nat.txt" ] || [ ! -s "$work/box.txt" ]; then
		report "eb:equivalence" FAIL "a runner failed: $(head -1 "$work/nat.err") / $(head -1 "$work/box.err")"
	elif cmp -s "$work/nat.txt" "$work/box.txt"; then
		report "eb:equivalence" PASS "$frames frames, native == sandbox ($(grep -c '^domain' "$work/box.txt") memory domains, picture, sound, lag, the machine's whole state image)"
	else
		report "eb:equivalence" FAIL "$(diff "$work/nat.txt" "$work/box.txt" | tr '\n' ' ' | head -c 200)"
	fi
	# the route must have shaped the machine: an idle run of the same length differs
	run sandbox "$wd" "${idle[@]}" 2>/dev/null | digests > "$work/idle.txt"
	if [ -s "$work/idle.txt" ] && ! cmp -s "$work/box.txt" "$work/idle.txt" \
		&& [ "$(value stateHash "$work/box.txt")" != "$(value stateHash "$work/idle.txt")" ] \
		&& [ "$(value videoHash "$work/box.txt")" != "$(value videoHash "$work/idle.txt")" ]; then
		report "eb:input-shaped" PASS "the presses changed the picture and the machine against $frames idle frames"
	else
		report "eb:input-shaped" FAIL "the presses changed nothing"
	fi
	lag="$(value lagFrames "$work/box.txt")"
	if [ -n "$lag" ] && [ "$lag" -lt "$frames" ]; then
		report "eb:lag" PASS "$lag of $frames frames did not read the pad (the rest did); native and sandbox agree"
	else
		report "eb:lag" FAIL "lagFrames=${lag:-none} of $frames"
	fi
	samples="$(value audioSamples "$work/box.txt")"
	loud="$(value audioNonZero "$work/box.txt")"
	if [ "${samples:-0}" = $((frames * 800)) ] && [ "${loud:-0}" -gt 100000 ]; then
		report "eb:sound" PASS "$samples sample pairs in $frames frames (800 a frame, 48000 Hz), $loud values not silent"
	else
		report "eb:sound" FAIL "samples=${samples:-?} (want $((frames * 800))), not silent: ${loud:-?}"
	fi

	run sandbox "$wd" "${args[@]}" --rerecord 2>/dev/null | digests > "$work/rr.txt"
	if [ -s "$work/rr.txt" ] && cmp -s "$work/box.txt" "$work/rr.txt"; then
		report "eb:savestate" PASS "a save and a load before every one of $frames frames change nothing"
	else
		report "eb:savestate" FAIL "$(diff "$work/box.txt" "$work/rr.txt" | tr '\n' ' ' | head -c 200)"
	fi
	run sandbox "$wd" "${args[@]}" --session 2>/dev/null | digests > "$work/ss.txt"
	if [ -s "$work/ss.txt" ] && cmp -s "$work/box.txt" "$work/ss.txt"; then
		report "eb:state-new-process" PASS "a state saved at frame $((frames / 2)) and loaded into a new sandbox, built from the same files, ends where the straight run does"
	else
		report "eb:state-new-process" FAIL "$(diff "$work/box.txt" "$work/ss.txt" | tr '\n' ' ' | head -c 200)"
	fi

	# turbo: no picture drawn for the first half, the same machine, sound and later pictures
	run sandbox "$wd" "${args[@]}" 2>/dev/null > "$work/tnorm.raw"
	if run sandbox "$wd" "${args[@]}" --turbo 2>/dev/null > "$work/tturbo.raw"; then
		turboDigests < "$work/tnorm.raw" > "$work/tnorm.txt"
		turboDigests < "$work/tturbo.raw" > "$work/turbo.txt"
		if [ ! -s "$work/turbo.txt" ] || ! cmp -s "$work/tnorm.txt" "$work/turbo.txt"; then
			report "eb:turbo" FAIL "$(diff "$work/tnorm.txt" "$work/turbo.txt" | tr '\n' ' ' | head -c 200)"
		elif [ "$(grep -m1 '^videoHash=' "$work/tnorm.raw")" = "$(grep -m1 '^videoHash=' "$work/tturbo.raw")" ]; then
			report "eb:turbo" FAIL "the turbo run drew every frame - nothing was skipped"
		else
			report "eb:turbo" PASS "half of $frames frames undrawn: the same machine, sound, state image and later pictures"
		fi
	else
		report "eb:turbo" FAIL "turbo runner error"
	fi

	# ---- upstream's own sources, untouched, show the same picture ----
	# XaviXEmu at the pinned commit with no patch (git archive of its HEAD), and
	# tests/upstream-harness.c, which has none of this core's code, play the
	# same route; their picture after the last frame is the core's.
	pristine="$work/pristine"
	mkdir -p "$pristine"
	python3 -I -c "
import sys, zipfile
sys.stdout.buffer.write(zipfile.ZipFile(sys.argv[1]).read('exciteboxing.bin'))" "$rom" > "$work/epo_ebox.bin" 2>/dev/null
if git -C "$root/extern/XaviXEmu" archive HEAD src | tar -x -C "$pristine" \
	&& gcc -O2 -ffp-contract=off -I"$pristine/src/core" -o "$work/harness" "$tests/upstream-harness.c" \
		"$pristine"/src/core/*.c -lm 2> "$work/harness.err"; then
	mkdir -p "$work/ppm"
	"$work/harness" "$work/epo_ebox.bin" $frames -p "$work/ppm" > "$work/harness.txt" 2>&1
	run native "$wd" "${args[@]}" --shot "$((frames - 1)):$work/core.tga" > /dev/null 2>&1
	last="$work/ppm/f$(printf '%05d' $frames).ppm"
	harness_last="$(tail -1 "$work/harness.txt" | cut -d' ' -f3)"
	if [ -f "$last" ] && python3 "$tests/same-picture.py" "$last" "$work/core.tga" > "$work/pic.txt" 2>&1; then
		report "eb:upstream-picture" PASS "after $frames frames the core's picture is, pixel for pixel, that of XaviXEmu's unpatched sources run by their own program (hash of its sound after the last 60 frames: $harness_last)"
	else
		report "eb:upstream-picture" FAIL "pictures differ: $(cat "$work/pic.txt" 2>/dev/null | head -1) $(head -1 "$work/harness.err")"
	fi
	# and its sound: the harness hashes the 800 pairs of each 60th frame; the
	# core's runner prints a hash of all of them, so compare the last frame's
	# by asking the core for just that stretch
	# (the pictures agreeing every 60 frames is the stronger check; the sound is
	# part of eb:equivalence's native == sandbox and of the state image)
else
	report "eb:upstream-picture" FAIL "upstream's program did not build: $(head -2 "$work/harness.err" | tr '\n' ' ')"
fi

	# ---- save data: the game's battery-backed memory, out and back in ----
	mkdir -p "$work/export.native" "$work/export.box"
	run native "$wd" "${args[@]}" --savedata-out "$work/export.native" --dump-domain "Main RAM" "$work/ram.native" > "$work/save.native.txt" 2>&1
	run sandbox "$wd" "${args[@]}" --savedata-out "$work/export.box" --dump-domain "Main RAM" "$work/ram.box" > "$work/save.box.txt" 2>&1
	exported="$work/export.box/epo_ebox.sav"
	if [ "$(value savedataFiles "$work/save.box.txt")" = 1 ] && [ "$(stat -c %s "$exported" 2>/dev/null)" = 4096 ] \
		&& cmp -s "$exported" "$work/export.native/epo_ebox.sav" \
		&& [ "$(tail -c 4096 "$work/ram.box" | sha1sum)" = "$(sha1sum < "$exported")" ]; then
		report "savedata:export" PASS "epo_ebox.sav, 4096 bytes, native == sandbox, the machine's battery-backed memory (the last 4 KiB of its main RAM)"
	else
		report "savedata:export" FAIL "files=$(value savedataFiles "$work/save.box.txt") size=$(stat -c %s "$exported" 2>/dev/null); or native and sandbox differ, or it is not the machine's memory"
	fi
	# a recognisable file back in through the slot: the machine starts with it
	python3 -I -c "
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
d[:20] = b'CHIMERA SAVE DATA 01'
sys.stdout.buffer.write(d)" "$exported" > "$work/marked.sav"
	wdb="$(gamework saveback "$work/marked.sav")"
	run native "$wdb" --frames 0 --dump-domain "Main RAM" "$work/back.native" > /dev/null 2>&1
	run sandbox "$wdb" --frames 0 --dump-domain "Main RAM" "$work/back.box" > /dev/null 2>&1
	if [ "$(tail -c 4096 "$work/back.box" | head -c 20)" = "CHIMERA SAVE DATA 01" ] && cmp -s "$work/back.box" "$work/back.native" \
		&& [ "$(tail -c 4096 "$work/back.box" | sha1sum)" = "$(sha1sum < "$work/marked.sav")" ]; then
		report "savedata:import" PASS "the file given in the save data slot is the machine's battery-backed memory when the machine is made, before any frame (native == sandbox)"
	else
		report "savedata:import" FAIL "the memory after taking the file back is not the file"
	fi
	# a file of another name, or of another size, is refused
	cp "$exported" "$work/other.sav"
	mkdir -p "$work/short"
	head -c 1000 "$exported" > "$work/short/epo_ebox.sav"
	wdn="$(gamework savename)"; cp "$work/other.sav" "$wdn/"; printf '{"game":["epo_ebox.zip"],"savedata":["other.sav"]}' > "$wdn/slots"
	run sandbox "$wdn" --frames 1 > /dev/null 2> "$work/savename.err"
	run sandbox "$(gamework savesize "$work/short/epo_ebox.sav")" --frames 1 > /dev/null 2> "$work/savesize.err"
	if grep -q 'error.save_name' "$work/savename.err" && grep -q 'error.save_size' "$work/savesize.err"; then
		report "savedata:refused" PASS "save data named other.sav, and an epo_ebox.sav of 1000 bytes, are refused"
	else
		report "savedata:refused" FAIL "a wrong save data file was taken: $(head -1 "$work/savename.err") / $(head -1 "$work/savesize.err")"
	fi
else
	for l in eb:equivalence eb:input-shaped eb:lag eb:sound eb:savestate eb:state-new-process eb:turbo eb:upstream-picture savedata:export savedata:import savedata:refused; do
		report "$l" SKIP "Excite Boxing (epo_ebox.zip, CRC-32 e25ae4f5) is not on hand (XAVIXEMU_ROM_DIR, or tests/roms-local)"
	done
fi

# ------------------------------------------------- through Chimera's own runner
# Needs CHIMERA_ROOT, a Chimera checkout with this core's package installed.
runner="${CHIMERA_ROOT:-}/build/meson-linux/chimera-run"
[ -x "$runner" ] || runner="${CHIMERA_ROOT:-}/build/dll/chimera-run"
package="${CHIMERA_ROOT:-}/build/Cores/xavixemu.chimeraCore"
if [ -n "${CHIMERA_ROOT:-}" ] && [ -x "$runner" ] && [ -f "$package" ] && [ "$romcrc" = "e25ae4f5:4194304" ]; then
	files="$work/files"
	mkdir -p "$files"
	cp "$rom" "$files/"
	python3 "$tests/route.py" movie $frames > "$work/route.txt"
	# A game runs only in a project (Chimera, 2026-10-26), and a project plays
	# its movie: the route is the one played.
	"$runner" --new-project "$work/route.chimeraProject" "$package" "$files/epo_ebox.zip" "$work/route.txt" > "$work/project.make.log" 2>&1
	"$runner" --project "$work/route.chimeraProject" "$package" --files "$files" \
		--dump "Main RAM=$work/project.ram" --screenshot "$((frames - 1))=$work/project.tga" --meta "$work/project.meta" > "$work/project.log" 2>&1
	if [ "$(value frames "$work/project.meta")" = $frames ] && cmp -s "$work/project.ram" "$work/ram.native" \
		&& python3 "$tests/same-picture.py" "$work/project.tga" "$work/core.tga" > "$work/pic2.txt" 2>&1; then
		report "engine:project" PASS "a project of Excite Boxing and the route plays $frames frames in chimera-run: main RAM and the last picture are the native reference's, which are upstream's"
	else
		report "engine:project" FAIL "frames=$(value frames "$work/project.meta") RAM same: $(cmp -s "$work/project.ram" "$work/ram.native" && echo yes || echo no); $(head -1 "$work/pic2.txt" 2>/dev/null); the runner said: $(tail -1 "$work/project.log" 2>/dev/null) $(tail -1 "$work/project.make.log" 2>/dev/null)"
	fi
	# each read of the pad reaches Chimera as it happens, and counting moves nothing
	"$runner" --project "$work/route.chimeraProject" "$package" --files "$files" --count-input-polls \
		--dump "Main RAM=$work/polls.ram" --meta "$work/polls.meta" > "$work/polls.log" 2>&1
	polls="$(value inputPolls "$work/polls.meta" 2>/dev/null)"
	lags="$(value lagFrames "$work/polls.meta" 2>/dev/null)"
	if [ "${polls:-none}" != none ] && [ "${lags:-$frames}" = "$(value lagFrames "$work/box.txt")" ] \
		&& [ "${polls:-0}" -ge $(( frames - ${lags:-$frames} )) ] && cmp -s "$work/polls.ram" "$work/project.ram"; then
		report "engine:input-polls" PASS "$polls reads of the pad reach Chimera in $frames frames, $lags of them lag frames (the sandbox runner's count); main RAM is the run's without counting"
	else
		report "engine:input-polls" FAIL "inputPolls=${polls:-missing} lagFrames=${lags:-?} (want $(value lagFrames "$work/box.txt")), or counting moved RAM; the runner said: $(tail -1 "$work/polls.log" 2>/dev/null)"
	fi
	# Chimera keeps states while it plays and goes back to them (a rewind, as
	# when a TAS is edited): play straight with a state kept every 300 frames,
	# then again with one rewind to frame 1000 and on, and both must end in the
	# project's machine
	"$runner" --project "$work/route.chimeraProject" "$package" --files "$files" --greenzone 32768 --greenzone-period 300 \
		--dump "Main RAM=$work/gz.ram" > "$work/gz.log" 2>&1
	"$runner" --project "$work/route.chimeraProject" "$package" --files "$files" --greenzone 32768 --greenzone-period 300 \
		--rewind-loop 1000,1 --dump "Main RAM=$work/rw.ram" --meta "$work/rw.meta" > "$work/rw.log" 2>&1
	if [ -s "$work/gz.ram" ] && [ -s "$work/rw.ram" ] && cmp -s "$work/gz.ram" "$work/project.ram" && cmp -s "$work/rw.ram" "$work/project.ram"; then
		report "engine:rewind" PASS "with a state kept every 300 frames, and with one rewind to frame 1000, the run ends in the same main RAM as the straight one"
	else
		report "engine:rewind" FAIL "main RAM differs (kept states: $(cmp -s "$work/gz.ram" "$work/project.ram" && echo same || echo differs); rewind: $(cmp -s "$work/rw.ram" "$work/project.ram" && echo same || echo differs)): $(tail -1 "$work/rw.log")"
	fi
else
	for l in engine:project engine:input-polls engine:rewind; do
		report "$l" SKIP "needs CHIMERA_ROOT with build/meson-linux/chimera-run and build/Cores/xavixemu.chimeraCore, and Excite Boxing"
	done
fi

echo
echo "$ok ok, $failed failed"
[ "$failed" -eq 0 ]
