#!/usr/bin/env python3
"""The route the tests play in Excite Boxing: what a person does from the title
screen on, as pads. It is the route of the first headless run of XaviXEmu's own
sources (tests/upstream-harness.c), so the two can be compared picture for
picture.

Idle for 400 frames, then in turns of 240 frames: OK for 4 frames, a direction
for 10 frames (right, down, left, up, right, down, ...), and, 120 frames in,
OK (or Back, in odd turns) for 4 frames.

  route.py movie <frames>   the route as a Chimera movie, one line a frame
  route.py press <frames>   the same as run-native's --press arguments
"""
import sys

# the machine's own bits: OK 0x01, Back 0x02, Up 0x10, Down 0x20, Left 0x40, Right 0x80
DIRS = [0x80, 0x20, 0x40, 0x10, 0x80, 0x20]


def pad(f):
    if f < 400:
        return 0
    t = f - 400
    ph, seg = t % 240, (t // 240) % 6
    if ph < 4:
        return 0x01
    if 60 <= ph < 70:
        return DIRS[seg]
    if 120 <= ph < 124:
        return 0x02 if seg & 1 else 0x01
    return 0


# the order of waterbox.config's buttons: Up Down Left Right OK Back Pause
BITS = [(0x10, "U"), (0x20, "D"), (0x40, "L"), (0x80, "R"), (0x01, "O"), (0x02, "B")]


def main():
    mode, frames = sys.argv[1], int(sys.argv[2])
    if mode == "movie":
        for f in range(frames):
            p = pad(f)
            print("||" + "".join(c if p & b else "." for b, c in BITS) + ".|")
    elif mode == "press":
        args = []
        for i, (b, _) in enumerate(BITS):
            start = None
            for f in range(frames + 1):
                on = f < frames and pad(f) & b
                if on and start is None:
                    start = f
                if not on and start is not None:
                    args.append("--press %d:%d:%d" % (i, start, f - start))
                    start = None
        print(" ".join(args))
    else:
        sys.exit("usage: route.py movie|press <frames>")


main()
