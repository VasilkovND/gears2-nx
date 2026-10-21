#!/usr/bin/env python3
"""Make the SD card folder for gears2-nx from your own Gears of War 2 disc.

    python3 stage_sd.py --disc "Gears of War 2.iso" -o <out>/gears2-nx
    python3 stage_sd.py --folder <already extracted disc> -o <out>/gears2-nx
    python3 stage_sd.py --check <game folder>        (only the checks below)
    python3 stage_sd.py --disc ... --tu <Title Update 6 package> -o <out>/gears2-nx   (also stages the update)

Writes, beside where the NRO goes (src/gow2_app.h reads this layout):
    gears2-nx/game/   the extracted disc: default.xex and GearGame/ (no file is over FAT32's 4 GB)

The same game/ folder is what tools/build_mac.sh takes to generate the code. The recompiled code is the retail
executable's with no title update (the PC port it comes from, gears-of-war-2-pc-port, supports only that one):
title ID 4D53082D, Media ID 63AF211E, XEX version 0.0.0.5. The executable's header is checked for those, and its
SHA-256 against the ones tested (KNOWN_XEX) when there are any.

Title Update 6 (--tu): not used by the build yet. The PC port's hints are for the executable without it, and TU6 changes
the code (default.xexp moves 0.0.0.5 to 0.0.6.5), so it waits until the base game runs. Staged apart, in
gears2-nx/tu6/: its default.xexp and the files the game reads from update: (TU__*.ini and localization, and
Patches/ScriptPatch_*.bin, UnrealScript fixes the engine loads). The package must be the one for the disc's Media ID:
each of the three retail executables (0.0.0.5, 0.0.0.6, 0.0.0.7) has its own TU6.
Only the standard library (the package is read by the small STFS reader below).
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
# Title Update 6: default.xexp SHA-256 -> (Media ID, the executable version it patches, the version it makes).
TU6 = {
    "f219c7dc96a4f06b4fcb75441fa554e177a95c05a3196042c03b9da01583337a": (0x63AF211E, "0.0.0.5", "0.0.6.5"),
    "83061c54922df83cc95684a7bd6fcea3192fe75fde17a066c5419b76bb78654d": (0x33FCE762, "0.0.0.6", "0.0.6.6"),
    "1bff3aa272dda26736dd05eda8fb8bff58fd211cbaa10e8369d91809dad2899f": (0x51D281BE, "0.0.0.7", "0.0.6.7"),
}


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


class Stfs:
    """The parts of an Xbox 360 STFS package (CON/LIVE/PIRS) needed to read one file from it."""

    BLOCK = 0x1000

    def __init__(self, path: Path):
        self.f = open(path, "rb")
        head = self.read(0, 0x3A0)
        if head[:4] not in (b"CON ", b"LIVE", b"PIRS"):
            sys.exit(f"{path} is not an Xbox 360 package (STFS)")
        self.title_id = struct.unpack_from(">I", head, 0x360)[0]
        header_size = struct.unpack_from(">I", head, 0x340)[0]
        self.data = (header_size + 0xFFF) & ~0xFFF
        volume = head[0x379:0x379 + 0x24]
        # Read-only packages keep one hash table per level, the others two.
        self.tables = 1 if volume[2] & 1 else 2
        self.table_blocks = struct.unpack_from("<H", volume, 3)[0]
        self.table_start = int.from_bytes(volume[5:8], "little")

    def read(self, offset: int, n: int) -> bytes:
        self.f.seek(offset)
        return self.f.read(n)

    def block(self, b: int) -> bytes:
        # Data blocks are numbered without the hash table blocks between them: one level 0 table every 0xAA blocks,
        # one level 1 every 0x70E4, one level 2 every 0x4AF768.
        backing = b + (b + 0xAA) // 0xAA * self.tables
        if b >= 0xAA:
            backing += (b + 0x70E4) // 0x70E4 * self.tables
            if b >= 0x70E4:
                backing += (b + 0x4AF768) // 0x4AF768 * self.tables
        return self.read(self.data + backing * self.BLOCK, self.BLOCK)

    def entries(self) -> list:
        out = []
        for k in range(self.table_blocks):
            table = self.block(self.table_start + k)
            for i in range(0, self.BLOCK, 0x40):
                e = table[i:i + 0x40]
                if not e[0]:
                    continue
                flags = e[0x28]
                out.append({"name": e[:flags & 0x3F].decode("latin-1"), "dir": bool(flags & 0x80),
                            "contig": bool(flags & 0x40), "blocks": int.from_bytes(e[0x29:0x2C], "little"),
                            "start": int.from_bytes(e[0x2F:0x32], "little"),
                            "parent": struct.unpack_from(">h", e, 0x32)[0],
                            "size": struct.unpack_from(">I", e, 0x34)[0]})
        return out



def stage_tu(package: Path, media_id: int, out: Path) -> None:
    """Every file of the TU package, as the game sees update:, into out/; default.xexp checked against TU6."""
    pkg = Stfs(package)
    try:
        if pkg.title_id != TITLE_ID:
            sys.exit(f"{package} is not a Gears of War 2 package (title {pkg.title_id:08X})")
        entries = pkg.entries()
        files = {}
        for e in entries:
            if e["dir"]:
                continue
            parent = entries[e["parent"]]["name"] + "/" if 0 <= e["parent"] < len(entries) else ""
            if "/" in e["name"] or "\\" in e["name"] or e["name"] in ("", ".", ".."):
                sys.exit(f"Unsafe name in package: {e['name']!r}")
            if not e["contig"]:
                sys.exit(f"{parent}{e['name']} is fragmented in this package: not supported")
            files[parent + e["name"]] = b"".join(pkg.block(e["start"] + k) for k in range(e["blocks"]))[:e["size"]]
    finally:
        pkg.f.close()
    xexp = files.get("default.xexp")
    if xexp is None:
        sys.exit(f"{package} has no default.xexp")
    known = TU6.get(hashlib.sha256(xexp).hexdigest())
    if not known:
        sys.exit(f"{package}: default.xexp is not one of the Title Update 6 packages this port knows")
    if known[0] != media_id:
        right = [k for k in TU6.values() if k[0] == media_id]
        sys.exit(f"{package} is TU6 for Media ID {known[0]:08X}, but the disc is {media_id:08X}"
                 + (f": use the package for {media_id:08X}" if right else ": no known TU6 for this disc"))
    for name, data in files.items():
        d = out / name
        d.parent.mkdir(parents=True, exist_ok=True)
        d.write_bytes(data)
    print(f"  Title Update 6 ({known[1]} -> {known[2]}): {len(files)} files -> {out} (not used by the build yet)")


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
    ap.add_argument("--tu", type=Path, help="Title Update 6 package for the disc's Media ID (staged in tu6/, unused yet)")
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
    if a.tu:
        if not a.tu.is_file():
            sys.exit(f"--tu: {a.tu} not found")
        stage_tu(a.tu, xex_info(game / "default.xex")["media_id"], a.out / "tu6")
    print(f"Ready: copy {a.out} to sdmc:/switch/ (the NRO and the shader files go in it, beside game/).")


if __name__ == "__main__":
    main()
