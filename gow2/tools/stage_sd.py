#!/usr/bin/env python3
"""Make the SD card folder for gears2-nx from your own Gears of War 2 disc.

    python3 stage_sd.py --disc "Gears of War 2.iso" -o <out>/gears2-nx
    python3 stage_sd.py --folder <already extracted disc> -o <out>/gears2-nx
    python3 stage_sd.py --check <game folder>        (only the checks below)

Writes, beside where the NRO goes (src/gow2_app.h reads this layout):
    gears2-nx/game/   the extracted disc: default.xex and GearGame/ (no file is over FAT32's 4 GB)

The same game/ folder is what tools/build_mac.sh takes to generate the code. The recompiled code is the retail
executable's with no title update (the PC port it comes from, gears-of-war-2-pc-port, supports only that one):
title ID 4D53082D, Media ID 63AF211E, XEX version 0.0.0.5. The executable's header is checked for those, and its
SHA-256 against the ones tested (KNOWN_XEX) when there are any; the console never gets a title update here.
Only the standard library.
"""
import argparse
import hashlib
import shutil
import struct
import sys
from pathlib import Path

TITLE_ID = 0x4D53082D
MEDIA_ID = 0x63AF211E
VERSION = "0.0.0.5"
# SHA-256 of default.xex dumps this port was built and tested with -> where it came from.
KNOWN_XEX: dict = {}
SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Disc files the game does not need: the console's system update.
SKIP = ("$systemupdate/",)
FAT32_MAX = (4 << 30) - 1


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(8 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


# --- XDVDFS (Xbox 360 disc) -------------------------------------------------------------------------------
def partition(f) -> int:
    for base in (0x0, 0x2080000, 0xFD90000, 0x18300000):
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return base
    sys.exit("Not an Xbox 360 disc image (no XDVDFS volume found)")


def walk(f, base, sector, size, prefix=""):
    f.seek(base + sector * SECTOR)
    table = f.read(size)
    pending, seen = [0], set()
    while pending:
        o = pending.pop()
        p = o * 4
        if o in seen or p + 14 > len(table):
            continue
        seen.add(o)
        left, right, start, length, attr, nlen = struct.unpack("<HHIIBB", table[p:p + 14])
        if left == 0xFFFF:
            continue
        name = table[p + 14:p + 14 + nlen].decode("latin1")
        if left:
            pending.append(left)
        if right:
            pending.append(right)
        if "/" in name or "\\" in name or name in ("", ".", ".."):
            sys.exit(f"Unsafe name on disc: {name!r}")
        path = f"{prefix}{name}"
        if attr & 0x10:
            if length:
                yield from walk(f, base, start, length, path + "/")
        else:
            yield path, start, length


def extract(iso: Path, out: Path) -> None:
    with iso.open("rb") as f:
        base = partition(f)
        f.seek(base + 32 * SECTOR + 20)
        root, root_size = struct.unpack("<II", f.read(8))
        files = sorted(e for e in walk(f, base, root, root_size) if not e[0].lower().startswith(SKIP))
        total = sum(n for _, _, n in files)
        done = 0
        for path, start, length in files:
            dest = out / path
            if dest.is_file() and dest.stat().st_size == length:
                done += length
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            f.seek(base + start * SECTOR)
            left = length
            with dest.open("wb") as o:
                while left:
                    chunk = f.read(min(8 << 20, left))
                    if not chunk:
                        sys.exit(f"{iso}: image ends inside {path}")
                    o.write(chunk)
                    left -= len(chunk)
                    done += len(chunk)
                    print(f"\r  {iso.name}: {done * 100 // max(total, 1)}%", end="", flush=True)
        print(f"\r  {iso.name}: {len(files)} files, {total / 2**30:.1f} GiB -> {out}")


# --- XEX header --------------------------------------------------------------------------------------------
def xex_info(path: Path) -> dict:
    """Title ID, Media ID and version from the XEX2 header's execution info (optional header 0x00040006)."""
    with path.open("rb") as f:
        head = f.read(0x1000)
    if head[:4] != b"XEX2":
        sys.exit(f"{path} is not an Xbox 360 executable (XEX2)")
    count = struct.unpack_from(">I", head, 0x14)[0]
    for i in range(min(count, 64)):
        key, value = struct.unpack_from(">II", head, 0x18 + i * 8)
        if key == 0x00040006:
            if value + 24 > len(head):
                with path.open("rb") as f:
                    f.seek(value)
                    info = f.read(24)
            else:
                info = head[value:value + 24]
            media_id, version, _base, title_id = struct.unpack_from(">IIII", info, 0)
            v = f"{version >> 28}.{(version >> 24) & 0xF}.{(version >> 8) & 0xFFFF}.{version & 0xFF}"
            return {"title_id": title_id, "media_id": media_id, "version": v,
                    "disc": info[18], "discs": info[19]}
    sys.exit(f"{path}: no execution info in the XEX header")


def check_game(game: Path) -> None:
    xex = game / "default.xex"
    if not xex.is_file():
        sys.exit(f"{xex} not found")
    if not (game / "GearGame").is_dir():
        sys.exit(f"{game}/GearGame not found: is this the Gears of War 2 disc?")
    info = xex_info(xex)
    if info["title_id"] != TITLE_ID:
        sys.exit(f"default.xex is title {info['title_id']:08X}, not Gears of War 2 ({TITLE_ID:08X})")
    if info["media_id"] != MEDIA_ID or info["version"] != VERSION:
        sys.exit(f"default.xex is Media ID {info['media_id']:08X} version {info['version']}; this port needs the retail "
                 f"disc's executable, Media ID {MEDIA_ID:08X} version {VERSION} (the PC port's code only matches that one)")
    digest = sha256_file(xex)
    if KNOWN_XEX and digest not in KNOWN_XEX:
        print(f"  warning: default.xex {digest} is not one of the tested dumps; trying anyway")
    print(f"  default.xex: Gears of War 2, Media ID {info['media_id']:08X}, version {info['version']}, sha256 {digest}")
    big = [p for p in game.rglob("*") if p.is_file() and p.stat().st_size > FAT32_MAX]
    if big:
        sys.exit(f"Files over 4 GB (FAT32 cannot hold them): {', '.join(str(p) for p in big)}")
    if (game / "default.xexp").exists():
        sys.exit(f"{game}/default.xexp: this port runs the executable without a title update; remove it")


def copy_folder(src: Path, dest: Path) -> None:
    n = 0
    for p in sorted(src.rglob("*")):
        rel = p.relative_to(src)
        if str(rel).lower().startswith(SKIP) or not p.is_file():
            continue
        d = dest / rel
        if d.is_file() and d.stat().st_size == p.stat().st_size:
            continue
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, d)
        n += 1
    print(f"  {n} files copied from {src} -> {dest}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--disc", type=Path, help="the disc image (ISO)")
    src.add_argument("--folder", type=Path, help="an already extracted disc (default.xex and GearGame/)")
    src.add_argument("--check", type=Path, help="only check a game folder (tools/codegen.sh uses it)")
    ap.add_argument("-o", "--out", type=Path)
    a = ap.parse_args()
    if a.check:
        check_game(a.check)
        return
    if not a.out:
        ap.error("-o/--out is required")
    game = a.out / "game"
    if a.disc:
        if not a.disc.is_file():
            sys.exit(f"--disc: {a.disc} not found")
        extract(a.disc, game)
    else:
        check_game(a.folder)
        copy_folder(a.folder, game)
    check_game(game)
    print(f"Ready: copy {a.out} to sdmc:/switch/ (the NRO and the shader files go in it, beside game/).")


if __name__ == "__main__":
    main()
