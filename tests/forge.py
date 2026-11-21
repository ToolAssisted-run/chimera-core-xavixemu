#!/usr/bin/env python3
"""Makes test content that is no game: a file of zeros with the SIZE and the
CRC-32 of a ROM set, so that the core's recognition and refusals can be tested
where no ROM may be passed on. The last four bytes are chosen to make the
CRC-32 come out as asked.

  forge.py <size> <crc32 hex> <out file> [zip member name]

With a member name the file is a ZIP holding that one member.
"""
import sys
import zipfile
import zlib


def table():
    t = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        t.append(c)
    return t


def forge(size, target):
    t = table()
    top = {t[i] >> 24: i for i in range(256)}
    r4 = target ^ 0xFFFFFFFF
    i4 = top[r4 >> 24]
    rem = r4 ^ t[i4]
    i3 = top[(rem >> 16) & 0xFF]
    rem ^= t[i3] >> 8
    i2 = top[(rem >> 8) & 0xFF]
    rem ^= t[i2] >> 16
    i1 = top[rem & 0xFF]
    prefix = bytes(size - 4)
    r = zlib.crc32(prefix) ^ 0xFFFFFFFF
    tail = bytearray()
    for i in (i1, i2, i3, i4):
        b = i ^ (r & 0xFF)
        tail.append(b)
        r = t[i] ^ (r >> 8)
    data = prefix + bytes(tail)
    assert zlib.crc32(data) == target, "forging failed"
    return data


def main():
    size, target, out = int(sys.argv[1], 0), int(sys.argv[2], 16), sys.argv[3]
    data = forge(size, target)
    if len(sys.argv) > 4:
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr(zipfile.ZipInfo(sys.argv[4], date_time=(1980, 1, 1, 0, 0, 0)), data)
    else:
        with open(out, "wb") as f:
            f.write(data)


main()
