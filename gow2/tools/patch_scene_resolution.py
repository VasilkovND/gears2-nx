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


def main(argv: list[str]) -> int:
    files: list[str] = []
    for a in argv:
        files.extend(glob.glob(a) if any(c in a for c in "*?[") else [a])
    done = 0
    for f in files:
        with open(f, errors="replace", newline="") as fh:
            lines = fh.readlines()
        start = next((i for i, l in enumerate(lines)
                      if l.startswith(f"DEFINE_REX_FUNC(sub_{FSTART:08X})")), None)
        if start is None:
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
        tmp = f + ".sr.tmp"
        with open(tmp, "w", errors="replace", newline="") as fh:
            fh.writelines(lines)
        os.replace(tmp, f)
        done += 1
    print(f"patch_scene_resolution: {done} site(s) patched")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
