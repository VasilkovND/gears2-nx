#!/usr/bin/env python3
"""gears2-nx - the 540p scene, as a game patch (id 8, gow2/src/gow2_game_patches.cpp).

sub_82806A80 builds the scene: the height is hard-coded (li r11,720) and the width is the aspect
XGetVideoMode reported (16/9 on the Switch) times 720.0 (lfs f0,2904(r10)), clamped to 900..1280
(li r11,1280 caps it). Written to the globals the target allocators read (0x830C9420 width,
0x830C9424 height), and the tile math below re-reads them, so patching where 720 enters keeps
pitch, scissor and EDRAM consistent: 540 -> 960x540, the same 16:9 shape.

Runs right after gears-of-war-2-pc-port's patch_game_patches.py (li r11,720 already carries its
id 5 there). The sites are located by their instruction comments inside sub_82806A80 (unique
there) and rewritten token-wise, so whatever register prefix the generated code uses at that
point (ctx., or L_ after patch_local_regs.py) is kept.

Usage: patch_scene_resolution.py <generated .cpp files>
"""

import glob
import os
import re
import sys

FSTART = 0x82806A80
MARK = "// GOW2 SCENE"
P = r"(?:ctx\.|L_)"  # a register prefix, either stage of the generated code
DECL = f'extern "C" int Gow2GamePatch(int id);  {MARK}\n'

# The display-mode table reads that build the menu / back buffer surfaces: the anchor is the last table
# load's instruction comment; the width and height registers are clamped after it.
# (function, anchor comment, width register, height register)
MENU_SITES = [
    (0x828041A8, "// lwzx r4,r6,r8", "r4", "r5"),    # the parameters handed to the D3D setup
    (0x828041A8, "// lwzx r11,r11,r29", "r11", "r20"),  # loc_828043A0: the same table, a second read
    (0x82803C90, "// lwz r7,0(r11)", "r10", "r7"),   # the whole-table scan: r10 the width, r7 the height
]


def rewrite(li720: str, lfs: list[str], li1280: str):
    h = re.fullmatch(P + r"r11\.s64 = (?:Gow2GamePatch\(5\) \? 704 : )?720;(?:\s*//.*)?", li720)
    if not h:
        return None, "'li r11,720' not in the expected shape"
    alt = "Gow2GamePatch(5) ? 704 : " in li720
    li720 = re.sub(r"\s*// GOW2 GAMEPATCH 5\s*$", "", li720)  # its mark does not survive the rewrite
    new1 = re.sub(r"= (?:Gow2GamePatch\(5\) \? 704 : )?720;",
                  "= Gow2GamePatch(8) ? 540 : (Gow2GamePatch(5) ? 704 : 720);" if alt
                  else "= Gow2GamePatch(8) ? 540 : 720;", li720)
    if len(lfs) != 3 or not re.fullmatch(
            P + r"fpscr\.disableFlushMode\(\);", lfs[0]) or not re.fullmatch(
            r"temp\.u32 = REX_LOAD_U32\(" + P + r"r10\.u32 \+ 2904\);", lfs[1]) or not re.fullmatch(
            P + r"f0\.f64 = double\(temp\.f32\);", lfs[2]):
        return None, "'lfs f0,2904(r10)' body not in the expected shape"
    new2 = lfs[0] + " " + lfs[1] + " " + lfs[2].replace(
        "double(temp.f32);", "double(Gow2GamePatch(8) ? 540.0f : temp.f32);")
    if not re.fullmatch(P + r"r11\.s64 = 1280;", li1280):
        return None, "'li r11,1280' not in the expected shape"
    new3 = li1280.replace("= 1280;", "= Gow2GamePatch(8) ? 960 : 1280;")
    return (new1, new2, new3), None


def escribir(f: str, lines: list[str]) -> None:
    """Write back, adding the Gow2GamePatch declaration a file that now calls it needs."""
    if DECL not in "".join(lines[:2]):
        lines.insert(0, DECL)
    tmp = f + ".sr.tmp"
    with open(tmp, "w", errors="replace", newline="") as fh:
        fh.writelines(lines)
    os.replace(tmp, f)


def menu_patch(lines: list[str]) -> tuple[int, str]:
    """The menu / back buffer layer: the D3D setup reads the display-mode table (rdata 0x82094010, the 720p
    entry {1280,720}) in three places, and the surfaces built from it stayed 1280x720 over the 960x540 scene,
    so the menu was cropped 1:1 in the composite. Clamp every read: the exact 1280/720 pair is the 720p mode,
    the other entries (0x100..0x200) are left alone. (The 1280x720 k_8 textures beside them are Bink video
    planes straight from the .bik header — those are not touched.)"""
    n = 0
    for fn, comment, rw, rh in MENU_SITES:
        start = next((i for i, l in enumerate(lines) if l.startswith(f"DEFINE_REX_FUNC(sub_{fn:08X})")), None)
        if start is None:
            continue
        end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("DEFINE_REX_FUNC")),
                   len(lines))
        j = next((i for i in range(start + 1, end) if lines[i].strip() == comment), None)
        if j is None or j + 1 >= end:
            return 0, f"'{comment}' not found in sub_{fn:08X}"
        got = lines[j + 1].strip()
        if not re.fullmatch(P + r"r\d+\.u64 = REX_LOAD_U32\(.*\);", got):
            return 0, f"'{comment}': body mismatch {got!r}"
        if j + 2 < end and MARK in lines[j + 2]:
            continue  # already patched
        v = "ctx." if got.startswith("ctx.") else "L_"
        clamp = (f"if (Gow2GamePatch(8)) {{ if ({v}{rw}.u32 == 1280) {v}{rw}.u32 = 960; "
                 f"if ({v}{rh}.u32 == 720) {v}{rh}.u32 = 540; }}")
        indent = re.match(r"\s*", lines[j + 1]).group(0)
        nl = "\r\n" if lines[j + 1].endswith("\r\n") else "\n"
        lines[j + 2:j + 2] = [f"{indent}{clamp}  {MARK}{nl}"]
        n += 1
    return n, ""


def main(argv: list[str]) -> int:
    files: list[str] = []
    for a in argv:
        files.extend(glob.glob(a) if any(c in a for c in "*?[") else [a])
    done = 0
    for f in files:
        with open(f, errors="replace", newline="") as fh:
            lines = fh.readlines()
        changed = False
        menu, err = menu_patch(lines)
        if err:
            print(f"patch_scene_resolution: {err}")
            return 1
        if menu:
            changed = True
            done += 1
        start = next((i for i, l in enumerate(lines)
                      if l.startswith(f"DEFINE_REX_FUNC(sub_{FSTART:08X})")), None)
        if start is None:
            if changed:
                escribir(f, lines)
            continue
        end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("DEFINE_REX_FUNC")),
                   len(lines))
        j = {}
        for want in ("li r11,720", "lfs f0,2904(r10)", "li r11,1280"):
            j[want] = next((i for i in range(start + 1, end) if lines[i].strip() == f"// {want}"), None)
            if j[want] is None:
                print(f"patch_scene_resolution: '// {want}' not found in sub_{FSTART:08X}")
                return 1
        if MARK in lines[j["li r11,720"] + 1]:
            continue  # already patched
        li720 = lines[j["li r11,720"] + 1].strip()
        lfs = [lines[j["lfs f0,2904(r10)"] + 1 + n].strip() for n in range(3)]
        li1280 = lines[j["li r11,1280"] + 1].strip()
        new, err = rewrite(li720, lfs, li1280)
        if err:
            print(f"patch_scene_resolution: {err}: {li720!r} {lfs!r} {li1280!r}")
            return 1
        def put(idx, old_count, text):
            indent = re.match(r"\s*", lines[idx + 1]).group(0)
            nl = "\r\n" if lines[idx + 1].endswith("\r\n") else "\n"
            lines[idx + 1:idx + 1 + old_count] = [f"{indent}{text}  {MARK}{nl}"]
        # From the last site to the first: an earlier replacement shifts the later indexes.
        put(j["li r11,1280"], 1, new[2])
        put(j["lfs f0,2904(r10)"], 3, new[1])
        put(j["li r11,720"], 1, new[0])
        changed = True
        escribir(f, lines)
        done += 1
    print(f"patch_scene_resolution: {done} site(s) patched")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
