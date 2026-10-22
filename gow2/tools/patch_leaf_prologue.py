#!/usr/bin/env python3
"""Leaf functions without the stack guard (make_nx_pch.py, point 5).

The guard in REX_FUNC_PROLOGUE() loads the KPCR's stack limit and compares it with r1 at the start of every recompiled
function. A function that calls nothing cannot deepen a recursion, so in those (the register save/restore helpers
__savegprlr_N / __restgprlr_N / __savefpr_N / __restfpr_N among them: 8.6 % of the game thread's time) the prologue
becomes REX_FUNC_PROLOGUE_LEAF(), the guard-free form. Anything that may call counts: any line passing "ctx, base",
an indirect call macro or a dispatch.

Usage: patch_leaf_prologue.py generated/default/*.cpp   (idempotent)
"""

import re
import sys

FUNC = re.compile(r"^(?:DEFINE_REX_FUNC|REX_WEAK_FUNC|PPC_FUNC_IMPL|REX_FUNC_IMPL)\b.*\{\s*$")
CALL = re.compile(r"ctx, base|REX_CALL|PPC_CALL|LookupFunction|->Call\(|Dispatch|dispatch")
PROLOGUE = "\tREX_FUNC_PROLOGUE();\n"
LEAF = "\tREX_FUNC_PROLOGUE_LEAF();\n"


def patch(path):
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        lines = f.readlines()
    leaves = total = 0
    i = 0
    while i < len(lines):
        if FUNC.match(lines[i]) and i + 1 < len(lines) and lines[i + 1] in (PROLOGUE, LEAF):
            total += 1
            j = i + 2
            calls = False
            while j < len(lines) and lines[j] != "}\n":
                if not calls and CALL.search(lines[j]):
                    calls = True
                j += 1
            want = PROLOGUE if calls else LEAF
            if not calls:
                leaves += 1
            lines[i + 1] = want
            i = j
        i += 1
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
        f.writelines(lines)
    return leaves, total


def main(paths):
    leaves = total = 0
    for p in paths:
        l, t = patch(p)
        leaves += l
        total += t
    print(f"patch_leaf_prologue: {leaves} of {total} functions are leaves (no stack guard)")
    return 0 if total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
