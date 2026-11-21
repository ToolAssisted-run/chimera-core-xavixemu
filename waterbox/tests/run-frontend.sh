#!/bin/bash
# The frontend half of the tests: load the XaviXEmu package in Chimera's
# window, run headless (no display), open a project of Excite Boxing with the
# route of tests/route.py as its movie, play it, and require main RAM and the
# last picture to be identical to the native reference. Then prove that the
# package's keybinds are the machine's when nobody changed them.
#
# Excite Boxing is not distributed with this repository: its ZIP is looked for
# in XAVIXEMU_ROM_DIR, or in tests/roms-local. Without it every check here
# says SKIP, because a project needs a game to be made at all.
#
# Usage: ./run-frontend.sh [--chimera-root <path>] [--frames N]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
frames=1800
chimera_root=""
while [ $# -gt 0 ]; do
	case "$1" in
		--chimera-root) chimera_root="$2"; shift ;;
		--frames) frames="$2"; shift ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done

if [ -z "$chimera_root" ] && [ -d "$root/../chimera" ]; then
	chimera_root="$root/../chimera"
fi
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || {
	echo "chimera checkout not found; pass --chimera-root <path>" >&2; exit 1; }
chimera_root="$(cd "$chimera_root" && pwd)"

chimera_exe="$chimera_root/build/meson-linux/source/ui/chimera"
package="$chimera_root/build/Cores/xavixemu.chimeraCore"
rn="$root/build/meson-native/run-native"
crun="$chimera_root/build/meson-linux/chimera-run"
[ -x "$crun" ] || crun="$chimera_root/build/dll/chimera-run"
[ -x "$chimera_exe" ] || { echo "Chimera not built: $chimera_exe" >&2; exit 1; }
[ -x "$crun" ] || { echo "chimera-run not built under $chimera_root/build" >&2; exit 1; }
[ -f "$package" ] || { echo "package not installed: $package (run ../build-package.sh)" >&2; exit 1; }
[ -x "$rn" ] || { echo "native reference not built" >&2; exit 1; }

work="$here/work"
rm -rf "$work"
mkdir -p "$work"

# the content: Excite Boxing, not distributed here
romdir="${XAVIXEMU_ROM_DIR:-$root/tests/roms-local}"
rom="$romdir/epo_ebox.zip"
if [ ! -f "$rom" ]; then
	echo "Excite Boxing (epo_ebox.zip) is not on hand (XAVIXEMU_ROM_DIR, or tests/roms-local): the frontend checks need it, and are skipped" >&2
	printf "%-24s %-9s %s\n" "frontend" "SKIP" "no game"
	exit 0
fi
cp "$rom" "$work/epo_ebox.zip"
python3 "$root/tests/route.py" movie "$frames" > "$work/route.txt"
route_press="$(python3 "$root/tests/route.py" press "$frames")"

# Chimera's window, run headless: the cores installed in the checkout, its
# settings, cache and recovery files the run's own, and nothing carried over
# from a Chimera installed on this machine
export ALSOFT_DRIVERS=null CHIMERA_CORES_DIR="$chimera_root/build/Cores" CHIMERA_QT_CACHE="$work/qt-cache" CHIMERA_QT_RECOVERY="$work/qt-recovery" \
	CHIMERA_QT_HASHES="$work/qt-file-hashes.txt" CHIMERA_QT_CHIMERA_SETTINGS="$work/no-chimera-config.ini"

ok=0
failed=0
report() { printf "%-24s %-9s %s\n" "$1" "$2" "$3"; case "$2" in PASS) ok=$((ok+1)) ;; *) failed=$((failed+1)) ;; esac; }
printf "%-24s %-9s %s\n" "Check" "Result" "Detail"
printf "%-24s %-9s %s\n" "-----" "------" "------"

# the window's settings: empty
printf '{}' > "$work/settings.json"

# A game runs only in a project (Chimera, 2026-10-26): game_project <tag>
# makes one of the game and the route and prints its path.
game_project() {
	local dir="$work/project.$1"
	rm -rf "$dir" && mkdir -p "$dir" && cp "$work/epo_ebox.zip" "$dir/"
	"$crun" --new-project "$dir/$1.chimeraProject" "$package" "$dir/epo_ebox.zip" "$work/route.txt" > "$dir/make.log" 2>&1 \
		&& echo "$dir/$1.chimeraProject"
}

# run_frontend <tag>: the route in the window, main RAM dumped
run_frontend() {
	local tag="$1"
	local job="$work/job.$tag.txt" project
	printf 'frames=%s\nout=%s\nmeta=%s\ndomain=Main RAM\n' "$frames" "$work/$tag.dump.bin" "$work/$tag.meta.txt" > "$job"
	project="$(game_project "$tag")" || { echo "no project made (see tests/work/project.$tag/make.log)" > "$work/$tag.log"; return 1; }
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout -s KILL 1500 "$chimera_exe" --headless \
		"--config=$work/settings.json" "--core=$package" \
		"--lua=$here/frontend-ram.lua" "--project=$project" ) > "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}

# --- the machine the window builds is the one the core tests signed off on ---
wd="$work/native.base"
rm -rf "$wd" && mkdir -p "$wd" && cp "$rom" "$wd/epo_ebox.zip"
printf '{"game":["epo_ebox.zip"]}' > "$wd/slots"
if ! "$rn" "$wd" --frames "$frames" $route_press --dump-domain "Main RAM" "$work/native.base.bin" > "$work/native.base.txt" 2>&1; then
	report "game:frontend" FAIL "native runner error (see tests/work/native.base.txt)"
elif ! run_frontend base; then
	report "game:frontend" FAIL "no OK meta (see tests/work/base.log)"
elif cmp -s "$work/native.base.bin" "$work/base.dump.bin"; then
	report "game:frontend" PASS "$frames frames of the route in the window, main RAM identical to the native reference ($(sed -n 's/^lag=//p' "$work/base.meta.txt") lag frames)"
else
	report "game:frontend" FAIL "main RAM differs from the native reference"
fi

# --- the bindings the package ships are the machine's when nobody changed them ---
job="$work/job.keys.txt"
printf 'out=%s/keys.controls.json\nmeta=%s/keys.meta.txt\n' "$work" "$work" > "$job"
if project="$(game_project keys)" && ( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout -s KILL 900 "$chimera_exe" --headless \
	"--config=$work/settings.json" "--core=$package" "--lua=$here/dump-controls.lua" "--project=$project" ) > "$work/keys.log" 2>&1 \
	&& grep -q "^status=OK" "$work/keys.meta.txt" 2>/dev/null; then
	if python3 "$here/check-keybinds.py" "$work/keys.controls.json" "$wb/default_keybinds.json" "XaviX" > "$work/keys.txt" 2>&1; then
		report "keybinds" PASS "$(cat "$work/keys.txt")"
	else
		report "keybinds" FAIL "$(head -1 "$work/keys.txt")"
	fi
else
	report "keybinds" FAIL "run did not report OK (see tests/work/keys.log)"
fi

echo
echo "$ok ok, $failed failed"
[ "$failed" -eq 0 ]
